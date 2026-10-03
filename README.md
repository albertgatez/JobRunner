# J.U.A.N.

## Propósito
**Job Utility for Administration of Nodes (J.U.A.N.)** es un software basado en Linux para ejecutar, monitorear, controlar y manegar trabajos local o remotamente sobre redes privadas, LAN, o VPN. Soporta Concurrencia, IPC, persistencia, recuparación, manejo de recursos, cancelación y comunicación por red.

## Miembros del equipo
- Alberto de Jesús Carlos Muro
- Daniel León Knight
- Fernando de Jesús Arreola Reyes
- Santiago De Alba Anaya

## Estado del Proyecto
**Avance 01 — Núcleo local**

El servidor (`jobrunner-server`) y el cliente de línea de comandos (`jobrunner-cli`) se comunican por un Unix socket local. Con esta versión es posible:

- Enviar un trabajo y obtener un identificador único.
- Ejecutarlo como un proceso separado.
- Consultar su estado y su código de salida.
- Listar los trabajos (con filtro por estado).
- Solicitar su cancelación.
- Manejar comandos y solicitudes inválidas sin terminar el servicio.

**Aún no implementado:** límite de concurrencia y cola, persistencia y recuperación tras reinicio, archivo de configuración, operación remota (LAN/VPN) y cierre del servidor sin procesos huérfanos. El detalle está en la sección *Limitations & Planned Features* del [manual de usuario](docs/user-guide/USER_MANUAL.md).

## Construcción
**Requisitos:** Linux, compilador compatible con C++20 (`g++` o `clang++`) y la biblioteca de cabeceras [nlohmann/json](https://github.com/nlohmann/json).

Desde la raíz del repositorio (según el [manual de usuario](docs/user-guide/USER_MANUAL.md#installation)):

```bash
mkdir -p build

# Servidor
g++ -std=c++20 -O2 \
  src/server/*.cpp src/common/*.cpp src/domain/*.cpp src/io/*.cpp \
  src/network/*.cpp src/process/*.cpp src/protocol/*.cpp \
  -I/usr/include/nlohmann -o ./build/jobrunner-server

# Cliente
g++ -std=c++20 -O2 \
  src/client/*.cpp src/protocol/*.cpp \
  -I/usr/include/nlohmann -o ./build/jobrunner-cli
```

Si `nlohmann/json` no está en `/usr/include`, añade `-I<ruta>` hacia el directorio que contiene la carpeta `nlohmann/`.

## Ejecución
```bash
./build/jobrunner-server    # terminal 1 (socket por defecto: /tmp/jobrunner.sock)
./build/jobrunner-cli /tmp/jobrunner.sock submit echo hola  # terminal 2
./build/jobrunner-cli /tmp/jobrunner.sock status 1
```

Todos los comandos (`submit`, `status`, `list`, `cancel`), los estados del trabajo y los códigos de salida del cliente están en el [manual de usuario](docs/user-guide/USER_MANUAL.md).