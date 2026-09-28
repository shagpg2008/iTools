#include "tools/comm/MqttProtocol.h"

#include <iostream>

int main()
{
    const std::vector<unsigned char> a = mqtt::EncodeRemainingLength(127);
    const std::vector<unsigned char> b = mqtt::EncodeRemainingLength(128);
    const std::vector<unsigned char> c = mqtt::EncodeRemainingLength(16384);
    if (a.size() != 1 || a[0] != 127 || b.size() != 2 || b[0] != 128 || b[1] != 1 ||
        c.size() != 3 || c[0] != 128 || c[1] != 128 || c[2] != 1)
    {
        std::cerr << "MQTT Remaining Length vectors failed\n"; return 1;
    }
    std::cout << "mqtt protocol tests passed\n"; return 0;
}
