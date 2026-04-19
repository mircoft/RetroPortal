# RetroPortal

Androidová aplikácia + natívna knižnica (CMake/NDK), Python agenti v priečinku `agents/`.

**Repozitár:** [github.com/mircoft/RetroPortal](https://github.com/mircoft/RetroPortal)

---

## Lokálny build APK (Windows)

**Potrebné:** JDK **17**, Android SDK (najjednoduchšie cez Android Studio → SDK Manager).

V koreňovom priečinku repozitára:

```bat
gradlew.bat assembleDebug
```

Výstupný APK:

`app\build\outputs\apk\debug\app-debug.apk`

Ak `JAVA_HOME` nie je nastavené, nastav ho na JDK z Android Studia, napr.:

`C:\Program Files\Android\Android Studio\jbr`

---

## Automatizácia buildu (bez klikania v štúdiu)

| Čo | Ako |
|----|-----|
| **Build na PC** | Terminál v koreňovom priečinku: `gradlew.bat assembleDebug` |
| **Build v cloude** | Každý **push** na `main` spustí [Android CI](.github/workflows/android-ci.yml) → v **Actions** stiahni artifact **app-debug-apk** |
| **Python agenti v CI** | Push, ktorý mení `agents/**`, spustí [Agents CI](.github/workflows/agents-ci.yml) (`pytest`) |

### „Všetko naraz“ na PC (jeden skript)

Z koreňa repozitára (PowerShell):

```powershell
.\scripts\run_all_checks.ps1
```

Voliteľne najprv `git pull`:

```powershell
.\scripts\run_all_checks.ps1 --pull
```

Potrebuje **`JAVA_HOME`** (pre Gradle) a **Python** v PATH (pre `pytest`). Ak Gradle preskočí, skontroluj premennú prostredia.

### Úplne automaticky podľa času (Windows)

1. **Spúšťač úloh** (`taskschd.msc`) → **Vytvoriť úlohu**.
2. Spúšťač: napr. denne alebo pri prihlásení.
3. Akcia: **Program** `powershell.exe`, argumenty:

   `-ExecutionPolicy Bypass -File "C:\Users\Mirco\Emulator\scripts\run_all_checks.ps1"`

4. Do úlohy môžeš pridať aj **push na GitHub** (potom sa spustí CI v cloude):

   ```powershell
   git -C "C:\Users\Mirco\Emulator" pull
   git -C "C:\Users\Mirco\Emulator" push
   ```

   (Vyžaduje uložené prihlásenie / SSH / uložený token.)

**Čo sa nespustí „samé“ bez tvojho nastavenia:** inštalácia hier, pripojenie telefónu, `adb`, ani spúšťanie APK na zariadení — to vždy potrebuje zariadenie alebo emulátor.

---

## Python agenti (lokálne spustenie)

1. **Nainštaluj Python 3.11+** z [python.org](https://www.python.org/downloads/) alebo cez Microsoft Store (zaškrtni **Add to PATH**).

2. **Inštalácia balíka** (z koreňa repozitára):

   ```bat
   cd agents
   pip install -e ".[dev]"
   ```

3. **Príkazy v PATH** (po inštalácii):

   | Príkaz | Účel |
   |--------|------|
   | `retroportal-hunter` | Manifest závislostí (`manifest.yaml`) |
   | `retroportal-launch` | ADB: push súborov, `am start`, meranie FPS cez SurfaceFlinger |

   Príklady:

   ```bat
   retroportal-hunter agents\examples\deps.example.yaml
   ```

   Pre **AutoLauncher** musí byť **`adb` v PATH** (Android SDK `platform-tools`) a pripojené zariadenie alebo emulátor:

   ```bat
   retroportal-launch --push cesta\k\súboru --launch --monitor --seconds 15
   ```

Podrobnejšie: [`agents/README.md`](agents/README.md).

---

## Lokálny build (Linux / macOS)

```bash
chmod +x gradlew
./gradlew assembleDebug
```
