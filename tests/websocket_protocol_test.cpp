#include "tools/comm/WebSocketProtocol.h"

#include <iostream>

int main()
{
    // RFC 6455 section 1.3 example.
    const std::string result = websocket::MakeAcceptKey("dGhlIHNhbXBsZSBub25jZQ==");
    if (result != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")
    {
        std::cerr << "WebSocket accept-key vector failed\n";
        return 1;
    }
    std::cout << "websocket protocol tests passed\n";
    return 0;
}
