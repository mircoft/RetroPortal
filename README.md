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
| **Python agenti v CI** | Spustí sa pri **push** zmien v `agents/**`; navyše denne podľa **cron** (viď workflow) a ručne cez **Run workflow** |

### Agent pipeline na GitHube („raz overím a potom nech to behá samé“)

1. **Jednorazová kontrola:** na GitHube **Actions** vyber workflow **Agents CI** → **Run workflow** → musí byť zelený.
2. **Potom automaticky:**
   - pri každom **push** do `main`, ktorý mení `agents/` alebo tento workflow;
   - **raz denne** (čas je v súbore [`agents-ci.yml`](.github/workflows/agents-ci.yml), úprava poľa `cron`);
   - **GitHub po ~60 dňoch neaktivity v repozitári** vypne cron — stačí nový commit alebo znova zapnúť workflow.

**Čo pipeline zatiaľ nerobí sama:** `retroportal-launch` (ADB na telefón) — na GitHub runneri nie je tvoje zariadenie; ten agent ostáva na PC alebo vlastnom runneri.

### „Všetko naraz“ na PC (jeden skript)

Z koreňa repozitára (PowerShell):

```powershell
.\scripts\run_all_checks.ps1
```

Voliteľne najprv `git pull`:

```powershell
.\scripts\run_all_checks.ps1 --pull
```

Skript si vie **sám doplniť `JAVA_HOME`**, ak máš Android Studio v predvolenom umiestnení (`...\Android Studio\jbr`). Ak nie, dočasne:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
.\scripts\run_all_checks.ps1
```

**Nepáruj príkazy:** testy sú zvlášť — `pytest agents/tests -v` — a skript je zvlášť (`.\scripts\run_all_checks.ps1`). Nie `pytest ... .\scripts\...`.

Potrebuje aj **Python** v PATH pre `pytest`.

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
