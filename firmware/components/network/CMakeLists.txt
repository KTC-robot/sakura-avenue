idf_component_register(SRCS "udp_socket.cpp" "wifi_station.cpp" INCLUDE_DIRS "include" REQUIRES protocol device lwip esp_wifi esp_event esp_netif)
