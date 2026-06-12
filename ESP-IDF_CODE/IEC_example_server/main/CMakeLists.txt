idf_component_register(
    SRCS
        "main.cpp"           # Application entry point (app_main)
 
    INCLUDE_DIRS
        "."                  # Expose current directory (optional, for local headers)
 
    REQUIRES
        iec104               # Our IEC 104 client component
)