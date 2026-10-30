# FRAGATIME v1.0

Plugin VST3 de efectos granulares para Windows 64 bits (FL Studio).
Carga un archivo .wav / .mp3 y lo convierte en una nube de granos, o granula el audio en vivo de la pista.

## Compilar con GitHub (sin instalar nada)
1. Crea un repositorio nuevo en github.com y sube el contenido de esta carpeta
   (CMakeLists.txt, carpeta Source y carpeta .github).
2. Abre la pestaña **Actions**. La compilacion tarda unos 10-15 minutos.
3. Con la palomita verde, entra a la ejecucion y descarga **FRAGATIME-VST3-Windows** (Artifacts).
4. Copia la carpeta FRAGATIME.vst3 a `C:\Program Files\Common Files\VST3`.
5. En FL Studio: Options > Manage plugins > Find installed plugins.

## Memoria
El archivo cargado se guarda en RAM (maximo 4 minutos, unos 90 MB). El resto del plugin usa pocos MB.
