# RetroPortal

Androidová aplikácia + natívna knižnica (CMake/NDK), Python agenti v priečinku `agents/`.

## Lokálny build (Windows)

**Potrebné:** JDK **17**, Android SDK (najjednoduchšie cez Android Studio → SDK Manager).

V koreňovom priečinku repozitára:

```bat
gradlew.bat assembleDebug
```

Výstupný APK (po úspešnom builde):

`app\build\outputs\apk\debug\app-debug.apk`

Ak `JAVA_HOME` nie je nastavené, nastav ho na JDK z Android Studia, napr.:

`C:\Program Files\Android\Android Studio\jbr`

Potom v tom istom termináli:

```bat
gradlew.bat assembleDebug
```

## Lokálny build (Linux / macOS)

```bash
chmod +x gradlew
./gradlew assembleDebug
```

## GitHub Actions

Po nahratí repozitára na GitHub sa pri **push** do vetiev `main` alebo `master` spustí workflow **Android CI**, ktorý zostaví `assembleDebug` a uloží APK ako artifact.

**Prvé nastavenie na GitHube:** vytvor nový repozitár, pridaj remote, `git push`. V záložke **Actions** uvidíš beh workflow.

## Python agenti

Vyžaduje Python **3.11+**. Z priečinka `agents/`:

```bash
pip install -e .
```

Podrobnosti v [`agents/README.md`](agents/README.md).
