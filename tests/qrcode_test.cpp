#include <iostream>
#include "third_party/qrcodegen/qrcodegen.hpp"

int main()
{
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText("Hello", qrcodegen::QrCode::Ecc::LOW);
    const bool ok = qr.getSize() == 21 && qr.getModule(0, 0) && !qr.getModule(1, 1) && qr.getModule(3, 3);
    if (!ok) std::cerr << "QR Code test failed" << std::endl;
    return ok ? 0 : 1;
}
