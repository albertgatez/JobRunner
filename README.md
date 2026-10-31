# J.U.A.N.

## Propósito
**Job Utility for Administration of Nodes (J.U.A.N.)** es un software basado en Linux para ejecutar, monitorear, controlar y manegar trabajos local o remotamente sobre redes privadas, LAN, o VPN. Soporta Concurrencia, IPC, persistencia, recuparación, manejo de recursos, cancelación y comunicación por red.

## Miembros del equipo
- Alberto de Jesús Carlos Muro
- Daniel León Knight
- Fernando de Jesús Arreola Reyes
- Santiago De Alba Anaya

## Estado del Proyecto
**Hito 2 — Núcleo local con persistencia y recuperación**

El servidor (`jobrunner-server`) y el cliente de línea de comandos (`jobrunner-cli`) se comunican por un Unix socket local. Con esta versión es posible:

- Enviar un trabajo y obtener un identificador único.
- Ejecutarlo como un proceso separado, respetando un límite predeterminado de 3 Jobs activos y una cola FIFO de hasta 10.
- Consultar su estado y su código de salida.
- Listar los trabajos (con filtro por estado).
- Solicitar su cancelación, con escalamiento de `SIGTERM` a `SIGKILL` si el proceso no termina en 5 segundos.
- Persistir el historial en SQLite y recuperar los Jobs en cola tras reiniciar.
- Cerrar el servidor sin dejar procesos de Job vivos: espera hasta 5 segundos y luego fuerza la terminación de los grupos pendientes.
- Manejar comandos y solicitudes inválidas sin terminar el servicio.

**Aún no implementado:** acceso remoto (LAN/VPN), control de acceso y archivo de configuración. El detalle está en la sección *Limitations & Planned Features* del [manual de usuario](docs/user-guide/USER_MANUAL.md).

## Construcción
**Requisitos:** Linux, compilador compatible con C++20 (`g++` o `clang++`) la biblioteca de cabeceras [nlohmann/json](https://github.com/nlohmann/json) y las cabeceras de SQLite 3 (Fedora: `sqlite-devel`, Debian/Ubuntu: `libsqlite3-dev`; con CMake se descargan solas si faltan).

Desde la raíz del repositorio:

```bash
cmake -S . -B build
cmake --build build -j
```

Consulta [Verificación](verif/README.md) para ejecutar las pruebas automáticas y los casos contra el servidor real.

## Ejecución
```bash
./build/jobrunner-server    # terminal 1 (socket por defecto: /tmp/jobrunner.sock, base en ./data/jobrunner.db)
./build/jobrunner-cli /tmp/jobrunner.sock submit echo hola  # terminal 2
./build/jobrunner-cli /tmp/jobrunner.sock status 1
```

Opciones del servidor: `--data-dir DIR` (directorio de la base SQLite) y `--reset-db` (borra todo el historial al arrancar); ver `./build/jobrunner-server --help`. El historial se guarda por defecto en `./data/jobrunner.db`.

Todos los comandos (`submit`, `status`, `list`, `cancel`), los estados del trabajo y los códigos de salida del cliente están en el [manual de usuario](docs/user-guide/USER_MANUAL.md).