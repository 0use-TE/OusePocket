#include "ouse_camera.h"
#include "bsp/esp-bsp.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

static int descriptor=-1;
static bool initialized,streaming,held,queue_error;
static void *buffers[2];
static size_t buffer_lengths[2];
static struct v4l2_buffer current;
static unsigned width,height;
static unsigned frames;
static size_t frame_bytes;
static esp_err_t stop(void)
{
    if(descriptor>=0){int type=V4L2_BUF_TYPE_VIDEO_CAPTURE;if(streaming){if(ioctl(descriptor,VIDIOC_STREAMOFF,&type))return ESP_FAIL;streaming=false;}if(close(descriptor))return ESP_FAIL;descriptor=-1;}
    if(initialized){esp_err_t error=esp_video_deinit();if(error!=ESP_OK)return error;initialized=false;}
    streaming=held=queue_error=false;
    for(unsigned index=0;index<2;++index){free(buffers[index]);buffers[index]=NULL;buffer_lengths[index]=0;}
    return ESP_OK;
}
static esp_err_t start(void)
{
    esp_err_t stopped=stop();if(stopped!=ESP_OK)return stopped;
    esp_video_init_csi_config_t csi={.sccb_config={.init_sccb=false,.i2c_handle=bsp_i2c_get_handle(),.freq=CONFIG_BSP_I2C_CLK_SPEED_HZ},.reset_pin=-1,.pwdn_pin=-1};
    esp_video_init_config_t config={.csi=&csi};initialized=true;esp_err_t error=esp_video_init(&config);
    if(error!=ESP_OK){ESP_LOGE("OuseCamera","Video initialization failed: %s",esp_err_to_name(error));stop();return error;}
    descriptor=open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME,O_RDONLY);if(descriptor<0){stop();return ESP_ERR_NOT_FOUND;}
    struct v4l2_format format={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE};
    if(ioctl(descriptor,VIDIOC_G_FMT,&format)){stop();return ESP_FAIL;}
    format.fmt.pix.pixelformat=V4L2_PIX_FMT_RGB565;
    if(ioctl(descriptor,VIDIOC_S_FMT,&format) || ioctl(descriptor,VIDIOC_G_FMT,&format)){stop();return ESP_FAIL;}
    width=format.fmt.pix.width;height=format.fmt.pix.height;frame_bytes=(size_t)width*height*2;
    if(!width || !height || width>1280 || height>960 || (format.fmt.pix.bytesperline && format.fmt.pix.bytesperline!=width*2) || format.fmt.pix.pixelformat!=V4L2_PIX_FMT_RGB565){stop();return ESP_ERR_NOT_SUPPORTED;}
    struct timeval timeout={.tv_usec=250000};if(ioctl(descriptor,VIDIOC_S_DQBUF_TIMEOUT,&timeout)){stop();return ESP_FAIL;}
    struct v4l2_requestbuffers request={.count=2,.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.memory=V4L2_MEMORY_USERPTR};
    if(ioctl(descriptor,VIDIOC_REQBUFS,&request) || request.count!=2){stop();return ESP_FAIL;}
    for(unsigned index=0;index<2;++index){
        struct v4l2_buffer buffer={.index=index,.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.memory=V4L2_MEMORY_USERPTR};
        if(ioctl(descriptor,VIDIOC_QUERYBUF,&buffer) || buffer.length<frame_bytes){stop();return ESP_FAIL;}
        buffers[index]=heap_caps_aligned_calloc(128,1,(buffer.length+127)&~127U,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!buffers[index]){stop();return ESP_ERR_NO_MEM;}
        buffer_lengths[index]=buffer.length;
        buffer.m.userptr=(uintptr_t)buffers[index];if(ioctl(descriptor,VIDIOC_QBUF,&buffer)){stop();return ESP_FAIL;}
    }
    int type=V4L2_BUF_TYPE_VIDEO_CAPTURE;if(ioctl(descriptor,VIDIOC_STREAMON,&type)){stop();return ESP_FAIL;}
    frames=0;streaming=true;ESP_LOGI("OuseCamera","Streaming %ux%u RGB565; buffers in PSRAM",width,height);return ESP_OK;
}
static void put(void)
{
    if(!held)return;
    if(current.index<2){current.m.userptr=(uintptr_t)buffers[current.index];current.length=buffer_lengths[current.index];if(ioctl(descriptor,VIDIOC_QBUF,&current))queue_error=true;}
    else queue_error=true;
    held=false;
}
static esp_err_t frame(ouse_camera_frame_t *out)
{
    if(!out || !streaming || held)return ESP_ERR_INVALID_STATE;
    if(queue_error)return ESP_FAIL;
    memset(&current,0,sizeof(current));current.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;current.memory=V4L2_MEMORY_USERPTR;
    if(ioctl(descriptor,VIDIOC_DQBUF,&current))return errno==EAGAIN || errno==ETIMEDOUT || errno==EIO ? ESP_ERR_TIMEOUT : ESP_FAIL;
    held=true;
    if(current.index>=2){put();return ESP_ERR_INVALID_SIZE;}
    if(current.bytesused<frame_bytes || (current.flags&V4L2_BUF_FLAG_ERROR)){put();return ESP_ERR_TIMEOUT;}
    if(!frames++)ESP_LOGI("OuseCamera","First frame received: %u bytes",(unsigned)current.bytesused);
    *out=(ouse_camera_frame_t){.pixels=buffers[current.index],.width=width,.height=height,.stride=width,.bytes=frame_bytes};return ESP_OK;
}
void ouse_board_camera_attach(void)
{
    const ouse_camera_driver_t driver={.start=start,.frame=frame,.put=put,.stop=stop};
    (void)ouse_camera_init(&driver);
}
