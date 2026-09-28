param(
    [string]$Source = "$PSScriptRoot\..\icons\itool-source.png",
    [string]$Output = "$PSScriptRoot\itool.ico"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-XpIconDib([System.Drawing.Image]$sourceImage, [int]$size) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.DrawImage($sourceImage, 0, 0, $size, $size)
    $graphics.Dispose()

    $stream = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter $stream
    $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
    $xorBytes = $size * $size * 4
    $maskBytes = $maskStride * $size

    $writer.Write([int]40)             # BITMAPINFOHEADER size
    $writer.Write([int]$size)
    $writer.Write([int]($size * 2))    # XOR image plus AND mask
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([int]0)              # BI_RGB: XP-compatible uncompressed DIB
    $writer.Write([int]($xorBytes + $maskBytes))
    $writer.Write([int]0); $writer.Write([int]0)
    $writer.Write([int]0); $writer.Write([int]0)

    $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
    $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                             [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $row = New-Object byte[] ($size * 4)
    $rows = New-Object 'System.Collections.Generic.List[byte[]]'
    for ($y = $size - 1; $y -ge 0; --$y) {
        [Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0, $y * $data.Stride), $row, 0, $row.Length)
        $copy = New-Object byte[] $row.Length
        [Array]::Copy($row, $copy, $row.Length)
        $null = $rows.Add($copy)
        $writer.Write($copy)
    }
    $bitmap.UnlockBits($data)

    foreach ($pixelRow in $rows) {
        $mask = New-Object byte[] $maskStride
        for ($x = 0; $x -lt $size; ++$x) {
            if ($pixelRow[$x * 4 + 3] -lt 128) {
                $maskIndex = [int][Math]::Floor($x / 8.0)
                $mask[$maskIndex] = [byte]($mask[$maskIndex] -bor (1 -shl (7 - ($x % 8))))
            }
        }
        $writer.Write($mask)
    }

    $bitmap.Dispose(); $writer.Flush()
    $result = $stream.ToArray()
    $writer.Dispose(); $stream.Dispose()
    return ,$result
}

$sourceImage = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source))
$sizes = @(16, 24, 32, 48, 64)
$images = New-Object 'System.Collections.Generic.List[byte[]]'
foreach ($size in $sizes) {
    $null = $images.Add((New-XpIconDib $sourceImage $size))
}
$sourceImage.Dispose()

$outputStream = New-Object IO.MemoryStream
$outputWriter = New-Object IO.BinaryWriter $outputStream
$outputWriter.Write([uint16]0); $outputWriter.Write([uint16]1); $outputWriter.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; ++$i) {
    $outputWriter.Write([byte]$sizes[$i]); $outputWriter.Write([byte]$sizes[$i])
    $outputWriter.Write([byte]0); $outputWriter.Write([byte]0)
    $outputWriter.Write([uint16]1); $outputWriter.Write([uint16]32)
    $outputWriter.Write([uint32]$images[$i].Length); $outputWriter.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $outputWriter.Write($image) }
$outputWriter.Flush()
[IO.File]::WriteAllBytes($Output, $outputStream.ToArray())
$outputWriter.Dispose(); $outputStream.Dispose()

Write-Output "Generated XP-compatible DIB icon: $Output"
