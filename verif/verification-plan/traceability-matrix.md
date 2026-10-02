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


| Req ID | Descripción breve                                                                 | Prioridad | Método | Caso(s) | Evidencia                        | Resultado | Defecto/Excepción |
| ------ | ---------------------------------------------------------------------------------- | --------- | ------- | ------- | -------------------------------- | --------- | ------------------ |
| RF-01. | Aceptar comando/programa y argumentos y devolver un ID único de Job               | Alta      | TEST    | TC-001  | `verif/results/<run-id>/TC-001/` | Pendiente | -                  |
| RF-02. | Validar solicitudes vacías, malformadas o no autorizadas y devolver mensaje útil | Alta      | TEST    | TC-001  | `verif/results/<run-id>/TC-001/` | Pendiente | -                  |
| RF-03. | Encolar Jobs cuando no existe capacidad inmediata de ejecución                    | Alta      | TEST    | TC-002  | `verif/results/<run-id>/TC-002/` | Pendiente | -                  |
| RF-04. | Ejecutar Jobs en procesos independientes sin bloquear nuevas solicitudes           |           |         |         |                                  |           |                    |
| RF-05. | Respetar el límite configurable de Jobs simultáneos                              | Alta      | TEST    | TC-002  | `verif/results/<run-id>/TC-002/` | Pendiente | -                  |

## 4.2 Ciclo de vida y control


| Req ID | Descripción breve                                                         | Prioridad | Método | Caso(s)        | Evidencia                                 | Resultado | Defecto/Excepción |
| ------ | -------------------------------------------------------------------------- | --------- | ------- | -------------- | ----------------------------------------- | --------- | ------------------ |
| RF-06. | Mantener estados QUEUED, RUNNING, SUCCEEDED, FAILED y CANCELED             | Alta      | TEST    | TC-003         | `verif/results/<run-id>/TC-003/`          | Pendiente | -                  |
| RF-07. | Registrar tiempos de recepción, inicio, finalización y código de salida | Alta      | TEST    | TC-003, TC-006 | `verif/results/<run-id>/{TC-003,TC-006}/` | Pendiente | -                  |
| RF-08. | Consultar estado y metadata de un Job por ID                               |           |         |                |                                           |           |                    |
| RF-09. | Listar Jobs y filtrar por estado                                           |           |         |                |                                           |           |                    |
| RF-10. | Cancelar Jobs en estado QUEUED o RUNNING y reflejar el resultado           |           |         |                |                                           |           |                    |

## 4.3 Salida, persistencia y recuperación


| Req ID | Descripción breve                                                            | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------------------------------------------------------------------- | --------- | ------- | ------- | --------- | --------- | ------------------ |
| RF-11. | Capturar stdout y stderr por separado y permitir consultarlos                 |           |         |         |           |           |                    |
| RF-12. | Conservar metadata y resultados mínimos después de reiniciar el servicio    |           |         |         |           |           |                    |
| RF-13. | Recuperar el estado persistido y marcar coherentemente los Jobs interrumpidos |           |         |         |           |           |                    |
| RF-14. | Registrar eventos y errores operativos con timestamp y Job ID                 |           |         |         |           |           |                    |

## 4.4 Operación y configuración


| Req ID | Descripción breve                                                                           | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | -------------------------------------------------------------------------------------------- | --------- | ------- | ------- | --------- | --------- | ------------------ |
| RF-15. | Iniciar y detener el servicio de forma controlada, sin aceptar Jobs nuevos durante el cierre |           |         |         |           |           |                    |
| RF-16. | Configurar directorio de datos, límite de concurrencia, interfaz y puerto                   |           |         |         |           |           |                    |
| RF-17. | Proporcionar ayuda CLI y códigos de salida apropiados                                       |           |         |         |           |           |                    |

## 4.5 Acceso remoto y privado


| Req ID | Descripción breve                                                              | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ------------------------------------------------------------------------------- | --------- | ------- | ------- | --------- | --------- | ------------------ |
| RF-18. | Permitir operación remota mediante protocolo documentado                       |           |         |         |           |           |                    |
| RF-19. | Mantener la misma semántica entre operación local y remota                    |           |         |         |           |           |                    |
| RF-20. | Restringir acceso remoto a las redes/direcciones LAN/VPN configuradas           |           |         |         |           |           |                    |
| RF-21. | Delimitar mensajes, identificar errores y tolerar lecturas/escrituras parciales |           |         |         |           |           |                    |
| RF-22. | Mantener el servicio y los Jobs ante la desconexión del cliente                |           |         |         |           |           |                    |

## 4.6 Administración mínima


| Req ID | Descripción breve                                                                | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | --------------------------------------------------------------------------------- | --------- | ------- | ------- | --------- | --------- | ------------------ |
| RF-23. | Rechazar y registrar solicitudes que excedan los límites configurados            |           |         |         |           |           |                    |
| RF-24. | Proporcionar resumen de salud (activo, en ejecución, en cola, errores recientes) |           |         |         |           |           |                    |

---

# 5. Requisitos No Funcionales

Mínimo cubrir los **requisitos no funcionales** obligatorios (RNF-01 - RNF-26).

## 5.1 Plataforma y construcción


| Req ID  | Descripción breve                                                 | Prioridad | Método | Caso(s) | Evidencia                        | Resultado | Defecto/Excepción |
| ------- | ------------------------------------------------------------------ | --------- | ------- | ------- | -------------------------------- | --------- | ------------------ |
| RNF-01. | Compilar y ejecutar en la distribución Linux declarada            |           |         |         |                                  |           |                    |
| RNF-02. | Construcción reproducible desde un clon limpio                    |           |         |         |                                  |           |                    |
| RNF-03. | Operar normalmente sin privilegios de root                         |           |         |         |                                  |           |                    |
| RNF-04. | Mantener al menos 3 Jobs simultáneos con límite configurado en 3 | Alta      | TEST    | TC-002  | `verif/results/<run-id>/TC-002/` | Pendiente | -                  |
| RNF-05. | Consultar estado en ≤ 1 s con 100 Jobs almacenados                |           |         |         |                                  |           |                    |
| RNF-06. | Soportar al menos 500 registros de Jobs sin pérdida de metadata   |           |         |         |                                  |           |                    |
| RNF-07. | No iniciar más procesos que el límite de concurrencia            | Alta      | TEST    | TC-002  | `verif/results/<run-id>/TC-002/` | Pendiente | -                  |

## 5.2 Confiabilidad y recuperación


| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ------------------ | --------- | ------- | ------- | --------- | --------- | ------------------ |
| RNF-08. |                    |           |         |         |           |           |                    |
| RNF-09. |                    |           |         |         |           |           |                    |
| RNF-10. |                    |           |         |         |           |           |                    |
| RNF-11. |                    |           |         |         |           |           |                    |
| RNF-12. |                    |           |         |         |           |           |                    |
| RNF-13. |                    |           |         |         |           |           |                    |
| RNF-14. |                    |           |         |         |           |           |                    |
| RNF-15. |                    |           |         |         |           |           |                    |
| RNF-16. |                    |           |         |         |           |           |                    |

## 5.3 Mantenibilidad


| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ------------------ | --------- | ------- | ------- | --------- | --------- | ------------------ |
| RNF-17. |                    |           |         |         |           |           |                    |
| RNF-18. |                    |           |         |         |           |           |                    |
| RNF-19. |                    |           |         |         |           |           |                    |
| RNF-20. |                    |           |         |         |           |           |                    |

## 5.4 Observabilidad y usabilidad


| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ------------------ | --------- | ------- | ------- | --------- | --------- | ------------------ |
| RNF-21. |                    |           |         |         |           |           |                    |
| RNF-22. |                    |           |         |         |           |           |                    |
| RNF-23. |                    |           |         |         |           |           |                    |

## 5.5 Red y comunicación


| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ------------------ | --------- | ------- | ------- | --------- | --------- | ------------------ |
| RNF-24. |                    |           |         |         |           |           |                    |
| RNF-25. |                    |           |         |         |           |           |                    |
| RNF-26. |                    |           |         |         |           |           |                    |

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
| Funcionales    |               30 |                   - |        0% |
| No funcionales |               34 |                   - |        0% |
| **Total**      |           **64** |               **-** |    **0%** |

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
