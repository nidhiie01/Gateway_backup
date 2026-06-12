idf_component_register(
    SRCS 
        "gateway_main.cpp" 
        
    # PRIV_REQUIRES spi_flash nvs_flash
    INCLUDE_DIRS 
        "." 
        ".." 
       
    REQUIRES 
        espressif__arduino-esp32 
        nvs_flash 
        mbedtls 
        esp_wifi 
        esp_netif 
        mqtt
        
        

)