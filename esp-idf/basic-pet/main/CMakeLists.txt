idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS
        "."
        "../../../libs/codex-pet-anim/include"
        "../../../assets/aomi/display-96-png"
        "../../../assets/bitomos-umi/display-96-png"
    REQUIRES
        esp_timer
        spiffs
        m5gfx
        m5unified
)
