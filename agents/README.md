# RetroPortal agents

## Inštalácia

```bash
cd agents
pip install -e ".[dev]"
```

Voliteľné (Playwright len pre výslovne povolené URL v manifeste):

```bash
pip install -e ".[playwright]"
playwright install chromium
```

## CLI príkazy

Po `pip install -e .` sú dostupné:

- **`retroportal-hunter`** — načíta YAML manifest so závislosťami, stiahne / overí SHA-256 (pozri `examples/deps.example.yaml`).
- **`retroportal-launch`** — ADB: hash-sync push, `am start`, polling FPS (`dumpsys SurfaceFlinger --latency`).

Príklad hunter:

```bash
retroportal-hunter examples/deps.example.yaml --cache ~/.retroportal/deps_cache
```

Príklad launcher (potrebuje `adb` a zariadenie):

```bash
retroportal-launch --push ./artifact.bin --launch --monitor --seconds 20
```

## Testy

```bash
pytest agents/tests -v
```

(spúšťaj z koreňa repozitára `RetroPortal/`, nie z priečinka `agents/`, ak nepoužívaš editable install z koreňa.)

Z koreňa repozitára:

```bash
pip install -e "./agents[dev]"
pytest agents/tests -v
```
