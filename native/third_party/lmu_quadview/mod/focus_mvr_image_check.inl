// Validate the saved HDR scene against the game's own focus resolve before skipping
// geometry. Both images are from the same eye and frame, before post-processing.
namespace image_check {
struct Sample {
    std::vector<float> rgb;
    unsigned width{}, height{};
};
float tone(unsigned short bits) {
    if (bits & 0x8000)
        return 0;
    const unsigned exp = bits >> 10, mantissa = bits & 1023;
    const float value = exp == 31 ? 0.0f : std::ldexp(float(mantissa + (exp ? 1024 : 0)), exp ? int(exp) - 25 : -24);
    return value / (1 + value);
}
bool read(ID3D11Texture2D *image, Sample &sample, ComPtr<ID3D11Texture2D>& staging) {
    D3D11_TEXTURE2D_DESC d{};
    image->GetDesc(&d);
    if (d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && d.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS)
        return false;
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = d.MiscFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (staging) {
        D3D11_TEXTURE2D_DESC cached{};
        staging->GetDesc(&cached);
        if (cached.Width != d.Width || cached.Height != d.Height || cached.Format != d.Format ||
            cached.ArraySize != d.ArraySize || cached.MipLevels != d.MipLevels ||
            cached.SampleDesc.Count != d.SampleDesc.Count || cached.SampleDesc.Quality != d.SampleDesc.Quality)
            staging.Reset();
    }
    if (!staging && FAILED(s.device->CreateTexture2D(&d, nullptr, &staging)))
        return false;
    sample.width = (d.Width + 7) / 8;
    sample.height = (d.Height + 7) / 8;
    sample.rgb.resize(size_t(sample.width) * sample.height * 3);
    s.ctx->CopyResource(staging.Get(), image);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(s.ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)))
        return false;
    size_t i = 0;
    for (unsigned y = 0; y < d.Height; y += 8) {
        const auto *row = reinterpret_cast<const unsigned short *>(static_cast<const unsigned char *>(map.pData) +
                                                                   y * map.RowPitch);
        for (unsigned x = 0; x < d.Width; x += 8)
            for (unsigned ch = 0; ch < 3; ++ch)
                sample.rgb[i++] = tone(row[x * 4 + ch]);
    }
    s.ctx->Unmap(staging.Get(), 0);
    return true;
}
void save(const Sample &sample, const wchar_t *label) {
    wchar_t dir[2048]{}, path[2200]{};
    const auto n = GetEnvironmentVariableW(L"LMU_QV_CAPTURE_DIR", dir, 2048);
    if (!n || n >= 2048)
        return;
    std::swprintf(path, 2200, L"%ls\\mvr-eye%u-%u-%ls.ppm", dir, s.eye, s.checked[s.eye], label);
    std::vector<unsigned char> data;
    data.reserve(sample.rgb.size());
    for (const auto v : sample.rgb)
        data.push_back(static_cast<unsigned char>(255 * std::pow(v, 1.0f / 2.2f)));
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    char header[64];
    int len = std::snprintf(header, sizeof(header), "P6\n%u %u\n255\n", sample.width, sample.height);
    DWORD written{};
    WriteFile(file, header, len, &written, nullptr);
    WriteFile(file, data.data(), DWORD(data.size()), &written, nullptr);
    CloseHandle(file);
}
bool matches(ID3D11Texture2D *reference) {
    Sample paired, native;
    ComPtr<ID3D11Texture2D> staging;
    const auto started = GetTickCount64();
    if (!read(s.focusImage.Get(), paired, staging) || !read(reference, native, staging) || native.rgb.empty() ||
        native.width != paired.width || native.height != paired.height)
        return false;
    const auto readbackMs = GetTickCount64() - started;
    double total = 0, error = 0, signal = 0;
    double nativeLuma = 0, pairedLuma = 0;
    constexpr double weights[]{.2126, .7152, .0722};
    unsigned large = 0;
    for (size_t i = 0; i < native.rgb.size(); ++i) {
        const auto a = native.rgb[i], b = paired.rgb[i];
        total += a;
        signal += b;
        nativeLuma += a * weights[i % 3];
        pairedLuma += b * weights[i % 3];
        error += std::abs(a - b);
        large += std::abs(a - b) > .12f;
    }
    const double mean = total / native.rgb.size(), relative = error / (std::max)(.000001, total);
    const double fraction = double(large) / native.rgb.size();
    if (s.checked[s.eye] < 4) {
        save(native, L"native");
        save(paired, L"paired");
    }
    // A consistent exposure error can fit inside the RGB error budget even
    // when the whole focus patch looks brighter. Compare against this eye's
    // own simultaneous native image; the other eye sees a different scene.
    const double brightnessBias = (pairedLuma - nativeLuma) / (std::max)(.000001, nativeLuma);
    const bool ok = mean > .003 && relative < .10 && fraction < .035 && std::abs(brightnessBias) < .02;
    char line[256];
    std::snprintf(line, sizeof(line),
                  "nvidia_mvr compare eye=%u sample=%u native_mean=%.5f paired_mean=%.5f relative_rgb_error=%.4f "
                  "large_diff_fraction=%.4f relative_luma_bias=%.4f accepted=%d readback_ms=%llu",
                  s.eye, s.checked[s.eye], mean, signal / native.rgb.size(), relative, fraction, brightnessBias, int(ok), readbackMs);
    log_line(line);
    return ok;
}
} // namespace image_check
