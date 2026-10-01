# ADR-001 — Lenguaje y herramientas

| Campo          | Valor                                                  |
| -------------- | ------------------------------------------------------ |
| Estado         | Propuesto                                              |
| Fecha          | 2026-09-30                                             |
| Autor          | Equipo JUAN                                            |
| Req. afectados | RNF-01, RNF-02, RNF-03, RNF-17, RNF-18, RNF-19, RNF-20 |
| Tipo           | Arquitectura / Tecnología                              |
## Contexto

JobRunner requiere ejecutarse sobre Linux y realizar operaciones relacionadas con procesos, señales, concurrencia, sockets, persistencia y manejo de recursos del sistema.

El lenguaje seleccionado debe permitir:

- Crear y administrar procesos.
- Manejar señales del sistema operativo.
- Implementar comunicación mediante sockets.
- Controlar concurrencia.
- Trabajar con archivos y SQLite.
- Implementar una arquitectura modular.
- Generar un ejecutable reproducible en Linux.
- Mantener un sistema compilable con advertencias estrictas.

Además del lenguaje de programación, se deben seleccionar herramientas para:

- Compilación.
- Construcción del proyecto.
- Gestión de dependencias.
- Pruebas automatizadas.
- Depuración.
- Formato de código.
- Análisis estático.
- Control de versiones.

Se consideran principalmente **C++** y **Rust**, ambos adecuados para programación de sistemas en Linux.

## Alternativas consideradas

### Alternativa 1 — C++

Utilizar C++ como lenguaje principal, con GCC o Clang como compilador y CMake como sistema de construcción.

**Ventajas:**

- Amplio soporte para programación de sistemas en Linux.
- Acceso directo a APIs POSIX.
- Manejo de procesos, señales, sockets y archivos mediante APIs ampliamente utilizadas.
- Amplia disponibilidad de bibliotecas.
- CMake permite una construcción reproducible.
- Herramientas maduras para depuración y análisis.
- Permite controlar explícitamente recursos y memoria.
- Buena integración con bibliotecas nativas de Linux.

**Desventajas y riesgos:**

- Mayor posibilidad de errores relacionados con memoria.
- Mayor responsabilidad del desarrollador en la gestión de recursos.
- Posibilidad de errores de concurrencia difíciles de detectar.
- Mayor complejidad de algunas operaciones de bajo nivel.
- Mayor atención para evitar fugas de recursos.

### Alternativa 2 — Rust

Utilizar Rust como lenguaje principal, empleando Cargo como sistema de construcción y gestión de dependencias.

**Ventajas:**

- Seguridad de memoria en tiempo de compilación.
- Sistema de tipos y ownership que reduce determinadas clases de errores.
- Buen soporte para concurrencia segura.
- Cargo simplifica la compilación y gestión de dependencias.
- `rustfmt` facilita el formato consistente.
- `clippy` proporciona análisis estático.
- Reduce determinadas categorías de errores de memoria.

**Desventajas y riesgos:**

- Curva de aprendizaje mayor para integrantes con poca experiencia.
- Algunas operaciones de bajo nivel pueden requerir `unsafe`.
- La interacción con determinadas APIs POSIX puede requerir mayor trabajo.
- Algunas integraciones con bibliotecas externas pueden requerir investigación adicional.
- El equipo debe asegurar que todos los integrantes puedan comprender y defender el código.

## Decisión

Se utilizará **C++ como lenguaje principal**.

La configuración inicial de herramientas será:

| Componente              | Herramienta      |
| ----------------------- | ---------------- |
| Lenguaje                | C++              |
| Compilador              | GCC              |
| Estándar                | C++20 o superior |
| Sistema de construcción | CMake            |
| Pruebas automatizadas   | Catch2           |
| Depuración              | GDB              |
| Control de versiones    | Git              |

CMake será utilizado para centralizar:

- Configuración de compilación.
- Dependencias.
- Opciones de compilación.
- Advertencias.
- Pruebas.
- Generación del ejecutable.

Se utilizarán opciones de compilación que permitan detectar errores durante el desarrollo. Como mínimo se evaluará el uso de:

```text
-Wall
-Wextra
-Wpedantic
```

El proyecto deberá evitar advertencias injustificadas.

Para la gestión de memoria y recursos se priorizará el uso de:

- RAII.
- Smart pointers cuando correspondan.
- Contenedores de la STL.
- Objetos con ownership claramente definido.

La selección de C++ deberá validarse mediante un prototipo antes de considerarse una decisión definitiva.

## Consecuencias

### Positivas

- Acceso directo a APIs POSIX necesarias para JobRunner.
- Herramientas para compilación, depuración y análisis.
- CMake permite reproducir el proceso de construcción.
- C++ permite utilizar RAII para mejorar la gestión de recursos.
- Facilita el control explícito de procesos, sockets y archivos.
- Permite utilizar bibliotecas nativas y de terceros.
- Facilita la interacción con mecanismos del sistema operativo Linux.
- Permite implementar los componentes del sistema sin depender de un servidor externo.

### Negativas y riesgos

- Existe mayor responsabilidad sobre la gestión de memoria y recursos.
- Los errores de memoria pueden provocar fallos difíciles de diagnosticar.
- Los errores de concurrencia pueden producir condiciones de carrera.
- El uso incorrecto de punteros puede provocar corrupción de memoria.
- Se requiere aplicar análisis estático y pruebas.
- El código deberá revisarse entre integrantes.
- Se deberá mantener una configuración de compilación consistente entre los ambientes de desarrollo y verificación.

## Evidencia / prototipo

Se realizará un prototipo mínimo que permita comprobar:

1. Compilación de un programa C++ en Linux.
2. Creación de un proceso hijo.
3. Espera y obtención del código de salida.
4. Manejo de señales.
5. Creación de un socket.
6. Escritura y lectura de archivos.
7. Ejecución de análisis estático.
8. Construcción desde un entorno limpio.

La evidencia deberá almacenarse en:

```text
verif/results/<run-id>/
```

Cada ejecución deberá identificar:

- Sistema operativo.
- Arquitectura.
- Compilador.
- Versión del compilador.
- Dependencias.
- Configuración.
- Comandos utilizados.
- Resultado de las pruebas.

## Requisitos afectados

|Requisito|Relación|
|---|---|
|RNF-01|El proyecto debe compilar y ejecutarse en Linux.|
|RNF-02|CMake permitirá documentar y reproducir la construcción.|
|RNF-03|El ejecutable deberá funcionar sin privilegios de root para operación normal.|
|RNF-17|El lenguaje y herramientas deben permitir una arquitectura modular.|
|RNF-18|Las interfaces y decisiones deberán documentarse.|
|RNF-19|Se utilizarán advertencias estrictas y análisis estático.|
|RNF-20|Las pruebas automatizadas deberán poder ejecutarse mediante un comando documentado.|