param(
    [string]$Source = (Join-Path $PSScriptRoot "icons/itool-source.png")
)

Add-Type -AssemblyName System.Drawing

$linuxDirectory = Join-Path $PSScriptRoot "linux"
$macDirectory = Join-Path $PSScriptRoot "macos"
New-Item -ItemType Directory -Force $linuxDirectory, $macDirectory | Out-Null

function New-SquarePngBytes([System.Drawing.Image]$image, [int]$size) {
    $bitmap = New-Object System.Drawing.Bitmap($size, $size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

        $scale = [Math]::Min($size / $image.Width, $size / $image.Height)
        $width = [int][Math]::Round($image.Width * $scale)
        $height = [int][Math]::Round($image.Height * $scale)
        $x = [int](($size - $width) / 2)
        $y = [int](($size - $height) / 2)
        $graphics.DrawImage($image, $x, $y, $width, $height)

        $stream = New-Object System.IO.MemoryStream
        try {
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            return $stream.ToArray()
        }
        finally {
            $stream.Dispose()
        }
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Write-BigEndianUInt32([System.IO.Stream]$stream, [uint32]$value) {
    $bytes = [BitConverter]::GetBytes($value)
    [Array]::Reverse($bytes)
    $stream.Write($bytes, 0, $bytes.Length)
}

$sourceImage = [System.Drawing.Image]::FromFile($Source)
try {
    [System.IO.File]::WriteAllBytes(
        (Join-Path $linuxDirectory "itool.png"),
        (New-SquarePngBytes $sourceImage 256))
    [System.IO.File]::WriteAllBytes(
        (Join-Path $macDirectory "itool.png"),
        (New-SquarePngBytes $sourceImage 512))

    $entries = @(
        @{ Type = "icp4"; Size = 16 },
        @{ Type = "icp5"; Size = 32 },
        @{ Type = "icp6"; Size = 64 },
        @{ Type = "ic07"; Size = 128 },
        @{ Type = "ic08"; Size = 256 },
        @{ Type = "ic09"; Size = 512 },
        @{ Type = "ic10"; Size = 1024 }
    )
    $chunks = foreach ($entry in $entries) {
        [pscustomobject]@{
            Type = $entry.Type
            Data = New-SquarePngBytes $sourceImage $entry.Size
        }
    }

    $totalLength = 8
    foreach ($chunk in $chunks) { $totalLength += 8 + $chunk.Data.Length }
    $output = [System.IO.File]::Create((Join-Path $macDirectory "itool.icns"))
    try {
        $header = [Text.Encoding]::ASCII.GetBytes("icns")
        $output.Write($header, 0, $header.Length)
        Write-BigEndianUInt32 $output $totalLength
        foreach ($chunk in $chunks) {
            $type = [Text.Encoding]::ASCII.GetBytes($chunk.Type)
            $output.Write($type, 0, $type.Length)
            Write-BigEndianUInt32 $output (8 + $chunk.Data.Length)
            $output.Write($chunk.Data, 0, $chunk.Data.Length)
        }
    }
    finally {
        $output.Dispose()
    }
}
finally {
    $sourceImage.Dispose()
}

Write-Host "Generated Linux PNG and macOS PNG/ICNS application icons."
