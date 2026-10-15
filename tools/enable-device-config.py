"""Update defaults and an existing local sdkconfig without invoking the compiler."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
config = {
    # Temporarily isolate the host-control RX pump during startup diagnosis.
    # ROM flashing and the normal UART console do not require this service.
    'BROOKESIA_SERVICE_USB_ENABLE_AUTO_REGISTER': 'n',
    'BROOKESIA_SYSTEM_CORE_ENABLE_USB_BRIDGE': 'n',
    # Brookesia P4's UI creates many small objects. Keep internal RAM for
    # libc/FreeRTOS locks and the Waveshare C6 SDIO DMA buffers.
    'SPIRAM_MALLOC_ALWAYSINTERNAL': '0',
    'SPIRAM_MALLOC_RESERVE_INTERNAL': '131072',
    # Supported by Hosted 2.12.3; requires the existing PSRAM XiP policy.
    'ESP_HOSTED_DFLT_TASK_FROM_SPIRAM': 'y',
    # Service/Asio callbacks use native pthread/libc/FreeRTOS synchronization.
    # Keep those worker stacks internal; large GUI/Core stacks stay in PSRAM.
    'BROOKESIA_SERVICE_MANAGER_WORKER_STACK_IN_EXT': 'n',
    'BROOKESIA_UTILS_THREAD_CONFIG_STACK_IN_EXT': 'n',
    # Pocket and native media explicitly bind to this scheduler, even when the
    # primary scheduler already has internal stacks (its upstream default is n).
    'BROOKESIA_SERVICE_MANAGER_SECONDARY_SCHEDULER_ENABLE': 'y',
    'LV_USE_SNAPSHOT': 'y',
    'BROOKESIA_HAL_ADAPTOR_ENABLE_SYSTEM_DEVICE': 'y',
    'BROOKESIA_HAL_ADAPTOR_SYSTEM_ENABLE_RESTART_IMPL': 'y',
    # The stock Board Manager/audio profile does not describe this Waveshare board.
    'BROOKESIA_HAL_ADAPTOR_SYSTEM_ENABLE_BOARD_INFO_IMPL': 'n',
    'BROOKESIA_HAL_ADAPTOR_SYSTEM_ENABLE_OTA_UPDATER_IMPL': 'n',
    'BROOKESIA_HAL_ADAPTOR_ENABLE_AUDIO_DEVICE': 'n',
    'BROOKESIA_SERVICE_AUDIO_ENABLE_AUTO_REGISTER': 'y',
    'BROOKESIA_SERVICE_AUDIO_PLAYBACK_VOLUME_DEFAULT': '40',
    'BROOKESIA_HAL_ADAPTOR_ENABLE_WIFI_DEVICE': 'y',
    'BROOKESIA_SERVICE_WIFI_ENABLE_AUTO_REGISTER': 'y',
    'BROOKESIA_HAL_ADAPTOR_ENABLE_NETWORK_DEVICE': 'y',
    'BROOKESIA_HAL_ADAPTOR_NETWORK_ENABLE_SNTP_CLIENT_IMPL': 'y',
    'BROOKESIA_SERVICE_SNTP_ENABLE_AUTO_REGISTER': 'y',
    'ESP_SYSTEM_EVENT_TASK_STACK_SIZE': '4096',
    'ESP_WIFI_REMOTE_ENABLED': 'y',
    'ESP_WIFI_REMOTE_LIBRARY_HOSTED': 'y',
    'SLAVE_IDF_TARGET_ESP32C6': 'y',
    'ESP_HOSTED_SDIO_HOST_INTERFACE': 'y',
    'ESP_HOSTED_SDIO_SLOT_1': 'y',
    'ESP_HOSTED_SDIO_SLOT_0': 'n',
    'ESP_HOSTED_SDIO_4_BIT_BUS': 'y',
    'ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ': '25000',
    'ESP_HOSTED_PRIV_SDIO_PIN_CLK_SLOT_1': '18',
    'ESP_HOSTED_PRIV_SDIO_PIN_CMD_SLOT_1': '19',
    'ESP_HOSTED_PRIV_SDIO_PIN_D0_SLOT_1': '14',
    'ESP_HOSTED_PRIV_SDIO_PIN_D1_4BIT_BUS_SLOT_1': '15',
    'ESP_HOSTED_PRIV_SDIO_PIN_D2_4BIT_BUS_SLOT_1': '16',
    'ESP_HOSTED_PRIV_SDIO_PIN_D3_4BIT_BUS_SLOT_1': '17',
    'ESP_HOSTED_SDIO_GPIO_RESET_SLAVE': '54',
    'ESP_HOSTED_HOST_RESTART_NO_COMMUNICATION_WITH_SLAVE': 'n',
    'BROOKESIA_HAL_ADAPTOR_ENABLE_BLE_DEVICE': 'n',
    'BROOKESIA_HAL_ADAPTOR_ENABLE_BT_DEVICE': 'n',
    # Bluetooth is disabled for this firmware.
    'BROOKESIA_SERVICE_BLE_ENABLE_AUTO_REGISTER': 'n',
    'BT_ENABLED': 'n',
    'BT_NIMBLE_ENABLED': 'n',
    'BT_BLUEDROID_ENABLED': 'n',
    'BT_CONTROLLER_DISABLED': 'y',
    'BT_CONTROLLER_ENABLED': 'n',
    'BT_NIMBLE_TRANSPORT_UART': 'n',
    'BT_NIMBLE_ROLE_PERIPHERAL': 'n',
    'BT_NIMBLE_ROLE_CENTRAL': 'n',
    'BT_NIMBLE_GATT_SERVER': 'n',
    'ESP_HOSTED_ENABLE_BT_NIMBLE': 'n',
    'ESP_HOSTED_NIMBLE_HCI_VHCI': 'n',
}

for name in ['sdkconfig.defaults', 'sdkconfig']:
    path = root / 'examples/brookesia-minimal' / name
    if not path.exists():
        continue
    lines = path.read_text(encoding='utf-8').splitlines()
    keys = {'CONFIG_' + key for key in config}
    def keep(line):
        if line == '# OusePocket system, custom BSP audio, C6 Wi-Fi and BLE':
            return False
        key = line.split('=', 1)[0]
        if line.startswith('# CONFIG_') and line.endswith(' is not set'):
            key = line[2:-11]
        return key not in keys
    lines = [line for line in lines if keep(line)]
    lines.append('# OusePocket system, custom BSP audio, C6 Wi-Fi and BLE')
    for key, value in config.items():
        if value == 'n' and name == 'sdkconfig':
            lines.append(f'# CONFIG_{key} is not set')
        else:
            lines.append(f'CONFIG_{key}={value}')
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Updated {path.relative_to(root)}')
