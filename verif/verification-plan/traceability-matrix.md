# Formato de la Matriz de trazabilidad — JobRunner

---

## 1. Propósito

Esta matriz relaciona los requisitos funcionales y no funcionales de JobRunner con los métodos de verificación, casos de prueba, evidencias y resultados.

La trazabilidad sigue la relación:

```text
Requisito
    ↓
Método de verificación
    ↓
Caso(s) de verificación
    ↓
Evidencia
    ↓
Resultado
```

La matriz debe mantenerse actualizada durante el desarrollo y utilizarse como referencia para determinar la cobertura de verificación del proyecto.

---

## 2. Métodos de verificación


| Código    | Método       | Descripción                                                                                |
| ---------- | ------------- | ------------------------------------------------------------------------------------------- |
| TEST       | Prueba        | El requisito se verifica mediante una ejecución controlada del sistema.                    |
| ANALYSIS   | Análisis     | El requisito se verifica mediante análisis técnico, mediciones o revisión de resultados. |
| INSPECTION | Inspección   | El requisito se verifica mediante revisión de código, configuración o documentación.    |
| DEMO       | Demostración | El requisito se demuestra mediante una ejecución observable del sistema.                   |

---

## 3. Resultados posibles


| Resultado | Significado                                                                 |
| --------- | --------------------------------------------------------------------------- |
| PASS      | El requisito fue verificado satisfactoriamente.                             |
| FAIL      | La verificación se ejecutó pero el resultado no cumple el requisito.      |
| BLOCKED   | La verificación no pudo ejecutarse por una dependencia o problema externo. |

---

# 4. Requisitos funcionales

Mínimo cubrir los **requisitos funcionales** obligatorios (RF-01 - RF-24).

## 4.1 Gestión de trabajos

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-01. | Aceptar comando/programa y argumentos y devolver un ID único de Job | Alta | TEST | TC-001 | `verif/results/run-2026-10-02-02/` | PASS | - |
| RF-02. | Validar solicitudes vacías, malformadas o no autorizadas y devolver mensaje útil | Alta | TEST | TC-001 | `verif/results/run-2026-10-02-02/` | PASS | - |
| RF-03. | Encolar Jobs cuando no existe capacidad inmediata de ejecución | Alta | TEST | TC-002 | `verif/results/run-2026-10-27-01/` | PASS | - |
| RF-04. | Ejecutar Jobs en procesos independientes sin bloquear nuevas solicitudes | Alta | TEST |  |  |  |  |
| RF-05. | Respetar el límite configurable de Jobs simultáneos | Alta | TEST | TC-002 | `verif/results/run-2026-10-27-01/` | PASS | - |

## 4.2 Ciclo de vida y control

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-06. | Mantener estados QUEUED, RUNNING, SUCCEEDED, FAILED y CANCELED | Alta | TEST | TC-003 | `verif/results/run-2026-10-02-02/` | PASS | - |
| RF-07. | Registrar tiempos de recepción, inicio, finalización y código de salida | Alta | TEST | TC-003, TC-006 | `verif/results/run-2026-10-02-02/` (TC-003) | Pendiente | TC-003: PASS; TC-006 sin ejecutar |
| RF-08. | Consultar estado y metadata de un Job por ID | Alta | TEST |  |  |  |  |
| RF-09. | Listar Jobs y filtrar por estado | Alta | TEST |  |  |  |  |
| RF-10. | Cancelar Jobs en estado QUEUED o RUNNING y reflejar el resultado | Alta | TEST |  |  |  |  |

## 4.3 Salida, persistencia y recuperación

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-11. | Capturar stdout y stderr por separado y permitir consultarlos | Alta | TEST |  |  |  |  |
| RF-12. | Conservar metadata y resultados mínimos después de reiniciar el servicio | Alta | TEST |  |  |  |  |
| RF-13. | Recuperar el estado persistido y marcar coherentemente los Jobs interrumpidos | Alta | TEST |  |  |  |  |
| RF-14. | Registrar eventos y errores operativos con timestamp y Job ID | Alta | TEST |  |  |  |  |

## 4.4 Operación y configuración

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-15. | Iniciar y detener el servicio de forma controlada, sin aceptar Jobs nuevos durante el cierre | Alta | TEST |  |  |  |  |
| RF-16. | Configurar directorio de datos, límite de concurrencia, interfaz y puerto | Alta | TEST |  |  |  |  |
| RF-17. | Proporcionar ayuda CLI y códigos de salida apropiados | Media | TEST |  |  |  |  |

## 4.5 Acceso remoto y privado

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-18. | Permitir operación remota mediante protocolo documentado | Alta | TEST / DEMO |  |  |  |  |
| RF-19. | Mantener la misma semántica entre operación local y remota | Alta | TEST |  |  |  |  |
| RF-20. | Restringir acceso remoto a las redes/direcciones LAN/VPN configuradas | Crítica | TEST / ANALYSIS |  |  |  |  |
| RF-21. | Delimitar mensajes, identificar errores y tolerar lecturas/escrituras parciales | Alta | TEST |  |  |  |  |
| RF-22. | Mantener el servicio y los Jobs ante la desconexión del cliente | Alta | TEST |  |  |  |  |

## 4.6 Administración mínima

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-23. | Rechazar y registrar solicitudes que excedan los límites configurados | Alta | TEST |  |  |  |  |
| RF-24. | Proporcionar resumen de salud (activo, en ejecución, en cola, errores recientes) | Media | TEST |  |  |  |  |

## 4.7 Casos límite y concurrencia (RF-25 – RF-30)

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-25. | Rechazar explícitamente nuevos Jobs cuando la cola esté llena | Alta | TEST |  |  |  |  |
| RF-26. | Mantener estado coherente ante dos cancelaciones concurrentes del mismo Job | Alta | TEST |  |  |  |  |
| RF-27. | Manejar solicitudes duplicadas o reenviadas sin estados contradictorios | Alta | TEST |  |  |  |  |
| RF-28. | Mantener estado coherente si el cliente se desconecta durante una solicitud | Alta | TEST |  |  |  |  |
| RF-29. | Detectar terminación inesperada del hijo, liberar recursos y registrar estado terminal | Alta | TEST |  |  |  |  |
| RF-30. | Aplicar política de escalamiento si un Job no termina tras la cancelación normal | Alta | TEST |  |  |  |  |

---

# 5. Requisitos No Funcionales

Mínimo cubrir los **requisitos no funcionales** obligatorios (RNF-01 - RNF-26).

## 5.1 Plataforma y construcción

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-01. | Compilar y ejecutar en la distribución Linux declarada | Alta | TEST |  |  |  |  |
| RNF-02. | Construcción reproducible desde un clon limpio | Alta | TEST / INSPECTION |  |  |  |  |
| RNF-03. | Operar normalmente sin privilegios de root | Alta | TEST |  |  |  |  |
| RNF-04. | Mantener al menos 3 Jobs simultáneos con límite configurado en 3 | Alta | TEST | TC-002 | `verif/results/run-2026-10-27-01/` | PASS | - |
| RNF-05. | Consultar estado en ≤ 1 s con 100 Jobs almacenados | Media | TEST |  |  |  |  |
| RNF-06. | Soportar al menos 500 registros de Jobs sin pérdida de metadata | Alta | TEST |  |  |  |  |
| RNF-07. | No iniciar más procesos que el límite de concurrencia | Alta | TEST | TC-002 | `verif/results/run-2026-10-27-01/` | PASS | - |

## 5.2 Confiabilidad y recuperación

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-08. | Una solicitud inválida o una desconexión no termina el servicio | Alta | TEST |  |  |  |  |
| RNF-09. | La terminación anormal de un Job no afecta a otros Jobs | Crítica | TEST |  |  |  |  |
| RNF-10. | Tras reinicio, no reportar RUNNING un proceso que ya no se controla | Alta | TEST |  |  |  |  |
| RNF-11. | Evitar archivos persistentes parcialmente actualizados | Alta | TEST / INSPECTION |  |  |  |  |
| RNF-12. | No escuchar en interfaces públicas por defecto | Crítica | INSPECTION / TEST |  |  |  |  |
| RNF-13. | Limitar acceso remoto a localhost, subredes privadas autorizadas o VPN | Crítica | TEST |  |  |  |  |
| RNF-14. | Validar longitud y formato de todos los mensajes de red | Alta | TEST |  |  |  |  |
| RNF-15. | No exponer secretos ni configuración sensible en los registros | Alta | INSPECTION / TEST |  |  |  |  |
| RNF-16. | Aplicar mínimo privilegio y documentar el modelo de amenazas básico | Alta | ANALYSIS / INSPECTION |  |  |  |  |

## 5.3 Mantenibilidad

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-17. | Código modular, con responsabilidades identificables y sin monolitos | Alta | INSPECTION |  |  |  |  |
| RNF-18. | Documentar interfaces públicas y decisiones relevantes | Alta | INSPECTION |  |  |  |  |
| RNF-19. | Compilar sin advertencias nuevas bajo las opciones acordadas | Alta | TEST / INSPECTION |  |  |  |  |
| RNF-20. | Ejecutar las pruebas automatizadas con un único comando documentado | Alta | TEST |  |  |  |  |

## 5.4 Observabilidad y usabilidad

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-21. | Errores con operación, causa y acción sugerida cuando sea posible | Media | TEST / INSPECTION |  |  |  |  |
| RNF-22. | La bitácora permite correlacionar una solicitud con su Job | Alta | TEST |  |  |  |  |
| RNF-23. | Un tercero instala, inicia, envía y consulta un Job solo con la guía | Alta | DEMO |  |  |  |  |

## 5.5 Red y comunicación

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-24. | Tolerar mensajes fragmentados y múltiples solicitudes secuenciales | Alta | TEST |  |  |  |  |
| RNF-25. | Liberar sockets y procesos hijos al cerrar | Alta | TEST |  |  |  |  |
| RNF-26. | Demostración remota solo en red privada o VPN, sin exposición a Internet | Crítica | INSPECTION / DEMO |  |  |  |  |

## 5.6 Consistencia y robustez (RNF-27 – RNF-34)

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-27. | Transiciones de estado consistentes ante solicitudes concurrentes | Alta | TEST |  |  |  |  |
| RNF-28. | Actualizaciones persistentes atómicas o recuperables | Alta | TEST / ANALYSIS |  |  |  |  |
| RNF-29. | Backpressure o rechazo explícito al alcanzar límites (cola, memoria, fds, clientes) | Alta | TEST |  |  |  |  |
| RNF-30. | No dejar procesos huérfanos tras un cierre o falla | Alta | TEST |  |  |  |  |
| RNF-31. | Recuperación que distingue pendientes, terminados e interrumpidos | Alta | TEST |  |  |  |  |
| RNF-32. | Versión de protocolo y rechazo explícito de mensajes incompatibles | Alta | TEST |  |  |  |  |
| RNF-33. | Pruebas de estrés que demuestren liberación de memoria, procesos, archivos y sockets | Alta | TEST / ANALYSIS |  |  |  |  |
| RNF-34. | Decisiones de seguridad, consistencia o recuperación con análisis de riesgo y prueba | Alta | INSPECTION / ANALYSIS |  |  |  |  |

---

# 6. Regla para actualizar la matriz

Cada vez que se modifique un requisito, caso de verificación o decisión técnica se deberá revisar esta matriz.

Si se agrega un requisito:

```text
Nuevo requisito
      ↓
Agregar fila
      ↓
Asignar método
      ↓
Crear/asignar TC
      ↓
Definir evidencia
      ↓
Ejecutar verificación
      ↓
Registrar PASS/FAIL/BLOCKED
```

Si se modifica un requisito existente, se deberá verificar si sus casos de prueba y evidencias siguen siendo válidos.

Los cambios de alcance deberán quedar registrados mediante el mecanismo de Change Request correspondiente.

---

# 7. Resumen de cobertura


| Categoría     | Total requisitos | Requisitos trazados | Cobertura |
| -------------- | ---------------: | ------------------: | --------: |
| Funcionales    |               30 |                   6 |       20% |
| No funcionales |               34 |                   2 |        6% |
| **Total**      |           **64** |               **8** |   **13%** |

> La cobertura anterior representa **trazabilidad documental**, no significa que los requisitos ya estén verificados.

---

# 8. Criterios de aceptación de la matriz

Antes de la entrega final se deberá verificar:

- [ ]  Todos los RF obligatorios tienen al menos un método de verificación.
- [ ]  Todos los RNF obligatorios tienen al menos un método de verificación.
- [ ]  Todos los requisitos tienen al menos un caso asociado.
- [ ]  Todos los casos tienen evidencia almacenada.
- [ ]  Los resultados `FAIL` tienen un defecto o excepción asociado.
- [ ]  Los resultados `BLOCKED` tienen una justificación.
- [ ]  No existen requisitos obligatorios sin trazabilidad.
- [ ]  No existen casos de verificación sin requisitos asociados.
- [ ]  Las evidencias corresponden a la versión/commit evaluado.
- [ ]  Los resultados finales fueron revisados.
- [ ]  Los casos críticos tienen evidencia reproducible.
- [ ]  La matriz coincide con la versión final de RF/RNF.

---
