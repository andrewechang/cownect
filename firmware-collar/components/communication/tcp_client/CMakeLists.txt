idf_component_register(
    SRCS "prototype_tcp_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lwip
    PRIV_REQUIRES log
)
