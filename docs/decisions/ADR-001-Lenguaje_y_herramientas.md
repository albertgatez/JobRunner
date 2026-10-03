# ADR-001 — Lenguaje y herramientas.

| Campo          | Valor                                                  |
| -------------- | ------------------------------------------------------ |
| Estado         | Aprobado                                               |
| Fecha          | 2026-09-30                                             |
| Autor          | Equipo JUAN                                            |
| Req. afectados | RNF-01, RNF-02, RNF-03, RNF-17, RNF-18, RNF-19, RNF-20 |
| Tipo           | Arquitectura / Tecnología                              |
## Contexto

JobRunner requiere un núcleo local capaz de recibir trabajos, asignarles un identificador único, ejecutar cada trabajo como un proceso separado, consultar su estado, listar trabajos, solicitar su cancelación y obtener su código de salida.

La implementación requiere interacción directa con mecanismos proporcionados por el sistema operativo Linux/POSIX, principalmente para:

- Creación y ejecución de procesos.
- Comunicación entre procesos.
- Manejo de señales.
- Espera y recuperación del estado de procesos.
- Manejo de descriptores de archivo.
- Comunicación mediante sockets locales.
- Control de entrada y salida de los procesos.
- Gestión de errores y códigos de salida.

Se requiere seleccionar un lenguaje y un conjunto de herramientas que permitan implementar el núcleo local de manera mantenible y que puedan utilizarse durante las siguientes etapas del proyecto.

Se consideran principalmente **C++** y **Rust**, ambos adecuados para programación de sistemas en Linux.

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
- `rustfmt` facilita el formato consistente.
- `clippy` proporciona análisis estático.
- Reduce determinadas categorías de errores de memoria.

**Desventajas y riesgos:**

- Curva de aprendizaje mayor para integrantes con poca experiencia.
- Algunas operaciones de bajo nivel pueden requerir `unsafe`.
- La interacción con determinadas APIs POSIX puede requerir mayor trabajo.
- Algunas integraciones con bibliotecas externas pueden requerir investigación adicional.
- El equipo debe asegurar que todos los integrantes puedan comprender y defender el código.

## Decisión

Se utilizará **C++ como lenguaje principal**.

La configuración inicial de herramientas será:

| Componente                           | Herramienta                                     |
| ------------------------------------ | ----------------------------------------------- |
| Lenguaje                             | C++                                             |
| Distribución Linux                   | Ubuntu / Fedora                                 |
| Compilador                           | GCC                                             |
| Estándar                             | C++20                                           |
| Sistema de construcción              | CMake                                           |
| Pruebas automatizadas                | Catch2                                          |
| Serialización y deserialización JSON | nlohmann/json                                   |
| Versiones fijadas                    | GCC 15, CMake 4, Catch2 3, nlohmann/json 3.12.0 |
| Depuración                           | GDB                                             |
| Control de versiones                 | Git                                             |

La biblioteca nlohmann/json se utilizará para el manejo de mensajes JSON del protocolo de JobRunner.


> Python 3 se usa únicamente como herramienta auxiliar en los scripts de verificación (`verif/scripts/`), para construir tramas crudas del protocolo en las pruebas de solicitudes inválidas. No forma parte del producto, que se implementa en C++20 con CMake y Catch2.

CMake será utilizado para centralizar:

- Configuración de compilación.
- Dependencias.
- Opciones de compilación.
- Advertencias.
- Pruebas.
- Generación del ejecutable.

Se utilizarán opciones de compilación que permitan detectar errores durante el desarrollo. Se compilará, como mínimo, con las opciones:

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

La decisión se validó con el prototipo del núcleo local descrito en la sección *Evidencia / prototipo*.

### Razones de la decisión

- Las operaciones centrales del proyecto (`fork`, `execvp`, `waitpid`, `epoll`, `signalfd`, `kill` sobre grupos de procesos) son APIs POSIX/Linux en C. C++ las usa directamente, sin bindings ni bloques `unsafe`, lo que reduce el código que cada integrante debe estudiar para poder defenderlo.
- Los riesgos de memoria y de recursos se mitigan con RAII (por ejemplo, destructores que cierran descriptores), contenedores de la STL, advertencias estrictas del compilador y análisis estático.
- nlohmann/json y Catch2 son bibliotecas maduras que se integran con CMake.
- Rust ofrece mayores garantías de memoria, pero su curva de aprendizaje y el trabajo adicional para integrar las APIs POSIX no se justifican para el alcance y el plazo del proyecto.
- El equipo ya ha trabajado con C++ en cursos previos y ninguno ha usado Rust en un proyecto de sistemas.

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

La decisión de utilizar C++20 cuenta con una **implementación inicial funcional del núcleo local de JobRunner**.

La implementación actual utiliza mecanismos POSIX para:

- Crear procesos separados para los trabajos.
- Ejecutar comandos mediante `exec`.
- Esperar la finalización de procesos.
- Manejar señales.
- Solicitar la cancelación de trabajos.
- Obtener códigos de salida.
- Gestionar entrada y salida de los procesos.
- Mantener comunicación local entre cliente y servidor.

La evidencia deberá almacenarse en:

```text
verif/results/<run-id>/
```

La evidencia deberá incluir, como mínimo:

1. Construcción exitosa del proyecto desde el repositorio.
2. Ejecución del servidor local.
3. Envío de un trabajo.
4. Obtención de su identificador.
5. Ejecución del proceso separado.
6. Consulta de su estado.
7. Obtención del código de salida.
8. Solicitud de cancelación.
9. Manejo de comandos inválidos sin terminar el servicio.

La integración completa de CMake y Catch2, así como la generación de evidencia automatizada mediante los scripts y casos de prueba definidos para el proyecto, se encuentra pendiente de completar cuando corresponda al estado actual del repositorio.

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