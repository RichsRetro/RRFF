# Compatibilidad de juegos en Windows — compilación experimental

Las correcciones comprueban los ID de título, hashes exactos de los ejecutables y bytes de destino esperados. Solo cambian las copias del disco de destino. Antes de copiar se muestra una lista combinada de correcciones. Las copias reconocidas que ya están corregidas no se vuelven a corregir.

| Juego | Último informe Xbox | Compilación actual |
|---|---|---|
| Alter Echo | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Melbourne Cup Challenge | falló en la pantalla de título | Diseño de cabecera del adaptador corregido; nueva prueba en Xbox pendiente. |
| Furious Karting | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| LMA Manager 2005 | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Manchester United Manager 2005 | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Monster Garage | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Rugby League | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Ski Racing 2006 | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Race Driver 2 | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Race Driver 3 | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Rainbow Six : Critical Hour | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| True Crime: New York City | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| Gun | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |
| MTX Mototrax | funcionamiento confirmado por el usuario | Se informó de que el perfil reconocido actual funciona en Xbox. |

Rich informó el 14 de octubre de 2026 de que todos los juegos del lote actual funcionaban excepto Melbourne. No se indicó el alcance de las pruebas; esto no afirma que se completaran los juegos. El diseño corregido del adaptador de Melbourne necesita otra prueba y podría conservar el anterior problema de entrada desde HDD. Usa su carpeta extraída original para la nueva corrección.

Se mantienen los perfiles existentes de versiones exactas de Midnight Club 3 y Enter the Matrix. Tony Hawk’s American Wasteland y Project 8: las carpetas originales reconocidas se ofrecen en la lista combinada y se convierten directamente en game.iso y un lanzador en el disco. Los originales de Windows no cambian. Las versiones suministradas funcionaron en Xbox con los hooks manuales de RAM; la nueva conversión por flujo y los hooks automáticos de inicio del dashboard aún necesitan una prueba de hardware. La conversión admite actualmente imágenes de un solo archivo de menos de 4 GiB y hashes de ejecutables reconocidos exactos. Las carpetas de imágenes existentes se copian normalmente.

Los nombres de archivo desconocidos e incompatibles se notifican, en lugar de renombrarlos a ciegas. Las fuentes de Windows y las copias de seguridad Master no cambian.

Si te resulta útil, invita a Rich a un café: https://buymeacoffee.com/richsretro
