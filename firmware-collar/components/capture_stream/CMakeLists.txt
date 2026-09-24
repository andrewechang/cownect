idf_component_register(
    SRCS "crc32.cpp"
         "capture_stream_encoder.cpp"
         "capture_stream_decoder.cpp"
         "synthetic_stream.cpp"
    INCLUDE_DIRS "include"
    REQUIRES data cn_config board
)
