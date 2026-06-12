idf_component_register(
    SRCS 
        "gateway_main.cpp" 
        "../modules/GatewayModule/gateway.cpp" 
        "../modules/InternetModule/InternetModule.cpp"
         "../modules/UIDModule/generate_uid.cpp"
        "../modules/LedController/LedController.cpp"
        "../modules/Button/button.cpp"
        "../modules/WiFiModule/WiFiModule.cpp"
        "../modules/ETH_RMII/RmiiEthernetModule.cpp"
        "../modules/ETH_SPI/SpiEthernetModule.cpp"
        "../modules/ETH_W5500/W5500EthernetModule.cpp"
        "../modules/SDcardModule/sdcard.cpp"
        "../modules/GSMModule/GSMModule.cpp"
        "../modules/NetworkManager/network_manager.cpp"
        "../modules/RTCController/RTCController.cpp"
    

    INCLUDE_DIRS 
        "." 
        ".." 
        "../modules" 
        "../modules/UIDModule"
        "../modules/LedController"
        "../modules/I2CManager"
        "../modules/GatewayModule" 
        "../modules/InternetModule"
        "../modules/MacroConfig"
        "../modules/Button"
        "../modules/WiFiModule"
        "../modules/ETH_RMII"
        "../modules/ETH_SPI"
        "../modules/ETH_W5500"
        "../modules/SDcardModule"
        "../modules/GSMModule"
        "../modules/NetworkManager"
        "../modules/RTCController"

        
    REQUIRES 
        nvs_flash 
        mbedtls 
        esp_wifi 
        esp_netif 
        spiffs
        esp_eth
        lwip
        esp_event
        console_cmd_ping
        espressif__esp_modem
        driver
        spi_flash
        fatfs

    # EMBED_TXTFILES 
    #     ${project_dir}/server_certs/ca_cert.pem
)
