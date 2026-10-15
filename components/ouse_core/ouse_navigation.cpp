// SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// Uses the archived native gesture helper without Brookesia runtime dependencies.
#include "private/navigation_gesture.hpp"
extern "C" bool ouse_navigation_back(int x,int y,int stop_x,int stop_y,int width,int height)
{
    return esp_brookesia::system::super::navigation::back_swipe(x,y,stop_x,stop_y,width,height,1.0f);
}

// Clock formatting ported from the archived shell_status.cpp.
#include <ctime>
#include <cstring>
extern "C" void ouse_navigation_clock(char *buffer)
{
    const auto time=std::time(nullptr);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time,&time);
#else
    localtime_r(&time,&local_time);
#endif
    if (local_time.tm_year<124 || std::strftime(buffer,8,"%H:%M",&local_time)==0) std::strcpy(buffer,"--:--");
}

// Launcher geometry ported from shell_app_launcher.cpp (112px items, 18px gaps).
extern "C" void ouse_navigation_grid(int available_width,int available_height,unsigned index,
                                      int *x,int *y,int *page,int *page_width)
{
    const int item_width=112,item_height=112,item_gap=18;
    const int launcher_columns=std::max(1,(available_width+item_gap)/(item_width+item_gap));
    const int launcher_grid_width=launcher_columns*item_width+(launcher_columns-1)*item_gap;
    const int rows=std::max(1,(available_height+item_gap)/(item_height+item_gap));
    const unsigned capacity=static_cast<unsigned>(rows*launcher_columns);
    const unsigned slot=index%capacity;
    *x=static_cast<int>(slot%launcher_columns)*(item_width+item_gap);
    *y=static_cast<int>(slot/launcher_columns)*(item_height+item_gap);
    *page=static_cast<int>(index/capacity); *page_width=launcher_grid_width;
}
