# TC-XXX — [Título del caso de verificación]

## 1. Identificación

| Campo                   | Valor                                                              |
| ----------------------- | ------------------------------------------------------------------ |
| ID                      | TC-XXX                                                             |
| Título                  | [Nombre descriptivo]                                               |
| Requisitos relacionados | [RF-XX, RNF-XX]                                                    |
| Tipo                    | [Unit / Integration / System / Robustness / Security / Acceptance] |
| Método                  | [TEST / ANALYSIS / INSPECTION / DEMO]                              |
| Prioridad               | [Alta / Media / Baja]                                              |
| Responsable             | [Nombre]                                                           |
| Revisor                 | [Nombre]                                                           |
| Estado                  | [PASS / FAIL / BLOCKED]                                            |
| Versión / Commit        | [Commit evaluado]                                                  |
| Fecha de ejecución      | [AAAA-MM-DD]                                                       |

---

## 2. Objetivo

[Describir qué comportamiento, requisito o propiedad del sistema se pretende verificar.]

---

## 3. Requisitos a verificar

### RF-XX — [Nombre del requisito]

[Descripción breve del requisito.]

### RNF-XX — [Nombre del requisito]

[Descripción breve del requisito, si aplica.]

---

## 4. Precondiciones

Antes de ejecutar el caso se debe cumplir:

-  JobRunner está compilado.
-  El servicio está configurado correctamente.
-  La configuración requerida está disponible.
-  La base de datos / archivos de prueba están preparados.
-  No existen procesos de pruebas anteriores ejecutándose.
-  [Otra condición necesaria.]

---

## 5. Entorno de prueba

| Elemento              | Valor                     |
| --------------------- | ------------------------- |
| Sistema operativo     | [Linux / (Ubuntu/Fedora)] |
| Arquitectura          | [x86_64 / ARM64]          |
| Compilador            | [GCC 15+]                 |
| Versión de JobRunner  | [Versión]                 |
| Commit                | [Hash]                    |
| `max_concurrent_jobs` | [Valor]                   |
| Capacidad de cola     | [Valor]                   |
| Puerto                | [Puerto]                  |
| Cliente               | [jobrunner-cli version]   |

---

## 6. Datos de prueba

Describir los datos utilizados durante la prueba.

### Entrada

```text
[Comando, argumentos, configuración, mensajes, etc.]
```

### Configuración relevante

```text
[Configuración utilizada]
```

### Condiciones especiales

[Por ejemplo: número de clientes, cantidad de Jobs, duración de Jobs, tamaño de mensajes, etc.]

---

## 7. Procedimiento

### Paso 1

**Acción:**

[Descripción exacta de la acción.]

**Resultado esperado:**

[Qué debería ocurrir.]

---

### Paso 2

**Acción:**

[Descripción exacta de la acción.]

**Resultado esperado:**

[Qué debería ocurrir.]

---

### Paso 3

**Acción:**

[Descripción exacta de la acción.]

**Resultado esperado:**

[Qué debería ocurrir.]

---

### Paso N

**Acción:**

[Descripción exacta de la acción.]

**Resultado esperado:**

[Qué debería ocurrir.]

---

## 8. Resultado esperado

La prueba debe cumplir con todos los criterios siguientes:

-  [Criterio esperado 1]
-  [Criterio esperado 2]
-  [Criterio esperado 3]
-  [Criterio esperado 4]

**Criterio para PASS:**

[Definir claramente qué condiciones deben cumplirse para considerar la prueba exitosa.]

**Criterio para FAIL:**

[Definir qué resultado constituye un incumplimiento.]

---

## 9. Resultado observado

> Completar únicamente después de ejecutar la prueba.

[Describir exactamente lo que ocurrió durante la ejecución.]

### Estados observados

|Elemento|Resultado|
|---|---|
|Estado inicial|[Valor]|
|Estado intermedio|[Valor]|
|Estado final|[Valor]|
|Código de salida|[Valor]|
|Tiempo observado|[Valor]|

### Observaciones

[Información adicional obtenida durante la ejecución.]

---

## 10. Evidencia

Registrar las evidencias generadas durante la ejecución.

| Evidencia            | Ubicación                    | Descripción   |
| -------------------- | ---------------------------- | ------------- |
| Log                  | `verif/results/<run-id>/...` | [Descripción] |
| Salida stdout        | `verif/results/<run-id>/...` | [Descripción] |
| Salida stderr        | `verif/results/<run-id>/...` | [Descripción] |
| Captura              | `verif/results/<run-id>/...` | [Descripción] |
| Resultado de comando | `verif/results/<run-id>/...` | [Descripción] |

### Comandos utilizados

```bash
[comando utilizado para ejecutar la prueba]
```

```bash
[comando utilizado para obtener/verificar evidencia]
```

---

## 11. Resultado de la verificación

**Resultado:** [Resultado]

Opciones:

- `PASS` — Todos los criterios se cumplieron.
- `FAIL` — Uno o más criterios no se cumplieron.
- `BLOCKED` — La prueba no pudo ejecutarse por una dependencia o bloqueo.

---

## 12. Observaciones

[Notas adicionales relevantes para interpretar el resultado.]

---

## 14. Revisión

|Campo|Valor|
|---|---|
|Ejecutado por|[Nombre]|
|Fecha de ejecución|[AAAA-MM-DD]|
|Revisado por|[Nombre]|
|Fecha de revisión|[AAAA-MM-DD]|
|Resultado revisado|[Sí/No]|

### Comentarios del revisor

[Comentarios, correcciones o validación del resultado.] 

---
