# Compliance

- Redistributables (Microsoft VC++, DirectX optional components, codecs) must come from **vendor installers**, **user-supplied media**, or **explicit allowlisted URLs** in a title manifest—not from unauthorized bulk scraping.
- Automated browsers (e.g., Playwright) must target **permissions-explicit endpoints** only (internal inventory, manifests, or documented APIs).
- Binaries shipped inside the APK must comply with each upstream license (GPL/LGPL/MIT, etc.).
