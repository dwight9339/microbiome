@echo off
rem Runs KiCad design-rule check and re-exports the board STEP used by hardware\enclosure.
rem Close the PCB editor first so the export sees the saved board.
setlocal
set "KICAD_CLI=C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
if not exist "%KICAD_CLI%" set "KICAD_CLI=kicad-cli"
cd /d "%~dp0"
"%KICAD_CLI%" pcb drc --severity-all --schematic-parity --format json -o drc.json prototype_1.kicad_pcb
"%KICAD_CLI%" pcb export step --force --subst-models -o prototype_1_pcb.step prototype_1.kicad_pcb
echo.
echo Done. drc.json and prototype_1_pcb.step are updated.
pause
