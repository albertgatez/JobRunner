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

| Código     | Método       | Descripción                                                                              |
| ---------- | ------------ | ---------------------------------------------------------------------------------------- |
| TEST       | Prueba       | El requisito se verifica mediante una ejecución controlada del sistema.                  |
| ANALYSIS   | Análisis     | El requisito se verifica mediante análisis técnico, mediciones o revisión de resultados. |
| INSPECTION | Inspección   | El requisito se verifica mediante revisión de código, configuración o documentación.     |
| DEMO       | Demostración | El requisito se demuestra mediante una ejecución observable del sistema.                 |

---

## 3. Resultados posibles

| Resultado | Significado                                                                                  |
| --------- | -------------------------------------------------------------------------------------------- |
| PASS      | El requisito fue verificado satisfactoriamente.                                              |
| FAIL      | La verificación se ejecutó pero el resultado no cumple el requisito.                         |
| BLOCKED   | La verificación no pudo ejecutarse por una dependencia o problema externo.                   |

---

# 4. Requisitos funcionales

Mínimo cubrir los **requisitos funcionales** obligatorios (RF-01 - RF-24).
## 4.1 Gestión de trabajos

| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-01. | Aceptar comando/programa y argumentos y devolver un ID único de Job | Alta      | TEST   | TC-001  | `verif/results/<run-id>/TC-001/`| -          | -                  |
| RF-02. | Validar solicitudes vacías, malformadas o no autorizadas y devolver mensaje útil | Alta      | TEST   | TC-002  | `verif/results/<run-id>/TC-002/`| -          | -                  |
| RF-03. | Encolar Jobs cuando no existe capacidad inmediata de ejecución | Alta      | TEST   | TC-003  | `verif/results/<run-id>/TC-003/`| -          | -                  |
| RF-04. | Ejecutar Jobs mediante procesos independientes sin bloquear nuevas solicitudes | Alta      | TEST   | TC-004  | `verif/results/<run-id>/TC-004/`| -          | -                  |
| RF-05. |                   |           |        |         |           |           |                   |
## 4.2 Ciclo de vida y control
| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-06. |                   |           |        |         |           |           |                   |
| RF-07. |                   |           |        |         |           |           |                   |
| RF-08. |                   |           |        |         |           |           |                   |
| RF-09. |                   |           |        |         |           |           |                   |
| RF-10. |                   |           |        |         |           |           |                   |
## 4.3 Salida, persistencia y recuperación
| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-11. |                   |           |        |         |           |           |                   |
| RF-12. |                   |           |        |         |           |           |                   |
| RF-13. |                   |           |        |         |           |           |                   |
| RF-14. |                   |           |        |         |           |           |                   |
## 4.4 Operación y configuración
| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-15. |                   |           |        |         |           |           |                   |
| RF-16. |                   |           |        |         |           |           |                   |
| RF-17. |                   |           |        |         |           |           |                   |
## 4.5 Acceso remoto y privado
| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-18. |                   |           |        |         |           |           |                   |
| RF-19. |                   |           |        |         |           |           |                   |
| RF-20. |                   |           |        |         |           |           |                   |
| RF-21. |                   |           |        |         |           |           |                   |
| RF-22. |                   |           |        |         |           |           |                   |
## 4.6 Administración mínima
| Req ID | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------ | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RF-23. |                   |           |        |         |           |           |                   |
| RF-24. |                   |           |        |         |           |           |                   |

---

# 5. Requisitos No Funcionales

Mínimo cubrir los **requisitos no funcionales** obligatorios (RNF-01 - RNF-26). 
## 5.1 Plataforma y construcción

| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-01. | Compilar y ejecutar en la distribución Linux declarada                  | Alta      | TEST   | TC-035  | `verif/results/<run-id>/TC-035/` | -          | -                 |
| RNF-02. |                   |           |        |         |           |           |                   |
| RNF-03. |                   |           |        |         |           |           |                   |
| RNF-04. |                   |           |        |         |           |           |                   |
| RNF-05. |                   |           |        |         |           |           |                   |
| RNF-06. |                   |           |        |         |           |           |                   |
| RNF-07. |                   |           |        |         |           |           |                   |


## 5.2 Confiabilidad y recuperación

| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-08. |                   |           |        |         |           |           |                   |
| RNF-09. |                   |           |        |         |           |           |                   |
| RNF-10. |                   |           |        |         |           |           |                   |
| RNF-11. |                   |           |        |         |           |           |                   |
| RNF-12. |                   |           |        |         |           |           |                   |
| RNF-13. |                   |           |        |         |           |           |                   |
| RNF-14. |                   |           |        |         |           |           |                   |
| RNF-15. |                   |           |        |         |           |           |                   |
| RNF-16. |                   |           |        |         |           |           |                   |


## 5.3 Mantenibilidad

| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-17. |                   |           |        |         |           |           |                   |
| RNF-18. |                   |           |        |         |           |           |                   |
| RNF-19. |                   |           |        |         |           |           |                   |
| RNF-20. |                   |           |        |         |           |           |                   |


## 5.4 Observabilidad y usabilidad

| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-21. |                   |           |        |         |           |           |                   |
| RNF-22. |                   |           |        |         |           |           |                   |
| RNF-23. |                   |           |        |         |           |           |                   |


## 5.5 Red y comunicación

| Req ID  | Descripción breve | Prioridad | Método | Caso(s) | Evidencia | Resultado | Defecto/Excepción |
| ------- | ----------------- | --------- | ------ | ------- | --------- | --------- | ----------------- |
| RNF-24. |                   |           |        |         |           |           |                   |
| RNF-25. |                   |           |        |         |           |           |                   |
| RNF-26. |                   |           |        |         |           |           |                   |

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

|Categoría|Total requisitos|Requisitos trazados|Cobertura|
|---|--:|--:|--:|
|Funcionales|30|-|0%|
|No funcionales|34|-|0%|
|**Total**|**64**|**-**|**0%**|

> La cobertura anterior representa **trazabilidad documental**, no significa que los requisitos ya estén verificados. 

---

# 8. Criterios de aceptación de la matriz

Antes de la entrega final se deberá verificar:

- [ ] Todos los RF obligatorios tienen al menos un método de verificación.
- [ ] Todos los RNF obligatorios tienen al menos un método de verificación.
- [ ] Todos los requisitos tienen al menos un caso asociado. 
- [ ] Todos los casos tienen evidencia almacenada.
- [ ] Los resultados `FAIL` tienen un defecto o excepción asociado.
- [ ] Los resultados `BLOCKED` tienen una justificación.
- [ ] No existen requisitos obligatorios sin trazabilidad.
- [ ] No existen casos de verificación sin requisitos asociados.
- [ ] Las evidencias corresponden a la versión/commit evaluado.
- [ ] Los resultados finales fueron revisados.
- [ ] Los casos críticos tienen evidencia reproducible.
- [ ] La matriz coincide con la versión final de RF/RNF.

---

