# Finished-product execution path (run_cownect_firmware) and the safe base initialization
# shared by app_main's bring-up tests and the final firmware.
idf_component_register(
    SRCS "cownect_app.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board data devtest scheduler power cn_config comm_manager esp_system freertos log
)
