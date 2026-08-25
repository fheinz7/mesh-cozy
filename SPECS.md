# Especificación del algoritmo de descubrimiento topológico y asignación automática de relays en Bluetooth Mesh

## 1. Propósito

Este documento especifica un algoritmo automático para:

1. Descubrir todos los nodos de una red Bluetooth Mesh ya aprovisionada.
2. Estimar la distancia mínima en saltos desde cada nodo hasta el gateway.
3. Ejecutar descubrimiento local de vecinos desde el gateway y desde todos los nodos.
4. Medir la calidad de los enlaces entre nodos mediante RSSI y tasa de recepción.
5. Centralizar las tablas de vecinos en el gateway.
6. Reconstruir un grafo bidireccional de la red.
7. Dividir la topología en anillos según su distancia al gateway.
8. Seleccionar automáticamente el conjunto mínimo de relays que mantenga cobertura y redundancia.
9. Configurar dinámicamente la característica Relay de Bluetooth Mesh.
10. Verificar que todos los nodos continúen accesibles.
11. Recuperar automáticamente la red si una selección de relays interrumpe la conectividad.

El algoritmo está orientado a una red Bluetooth Mesh basada en managed flooding, sin routing explícito, utilizando nRF Connect SDK 2.9.0 y nodos nRF52840 ya aprovisionados.

---

## 2. Alcance

El algoritmo cubre:

- Descubrimiento de vecinos directos usando mensajes con `TTL=0`.
- Descubrimiento de distancia al gateway usando TTL creciente.
- Medición de RSSI.
- Estimación de Packet Delivery Ratio, PDR.
- Construcción del grafo topológico en el gateway.
- Clasificación de nodos por anillos.
- Selección greedy de relays.
- Configuración remota de Relay mediante Configuration Client Model.
- Verificación y rollback.
- Redescubrimiento periódico o bajo demanda.
- Recuperación frente a pérdida de relays.

El algoritmo no implementa routing por caminos fijos. Bluetooth Mesh continúa operando mediante managed flooding, pero solamente un subconjunto controlado de nodos retransmite los mensajes.

---

## 3. Suposiciones

1. Todos los nodos están aprovisionados.
2. Todos los nodos tienen una dirección unicast válida.
3. Todos comparten una NetKey válida.
4. El modelo de descubrimiento posee una AppKey configurada.
5. Todos los nodos están suscritos al grupo de control.
6. Los nodos con capacidad de relay incluyen:
   - Configuration Server Model.
   - Soporte de Relay compilado.
7. El gateway incluye:
   - Configuration Client Model.
   - Acceso a la NetKey y AppKey necesarias.
8. Durante el bootstrap existe conectividad completa porque:
   - Todos los candidatos comienzan como relays, o
   - Existe una infraestructura inicial de relays dedicados.
9. El gateway conoce las direcciones de los nodos aprovisionados.
10. El descubrimiento se ejecuta como operación de mantenimiento y no simultáneamente con el tráfico normal de datos.

---

## 4. Objetivos de diseño

### 4.1 Objetivos principales

- Evitar que todos los nodos permanezcan como relays.
- Reducir duplicados generados por managed flooding.
- Reducir colisiones y ocupación de los canales de advertising.
- Mantener conectividad completa.
- Mantener al menos dos opciones de relay por nodo cuando la topología lo permita.
- Detectar enlaces débiles o unidireccionales.
- Adaptarse automáticamente a cambios de topología.

### 4.2 Propiedades requeridas

El resultado debe garantizar, cuando la topología física lo permita:

- Todos los nodos alcanzables desde el gateway.
- Todo nodo no directo cubierto por al menos dos relays seleccionados.
- Todo relay conectado con al menos un relay del anillo anterior.
- Preferiblemente dos enlaces independientes hacia el gateway.
- Ningún cambio de configuración debe cortar el acceso a nodos pendientes de configurar.
- Toda asignación debe verificarse antes de ser confirmada.

---

## 5. Terminología

### Gateway

Nodo raíz de la topología. Ejecuta el Configuration Client, centraliza los reportes, calcula los anillos y selecciona los relays.

### Nodo común

Nodo que genera y consume datos, pero normalmente permanece con Relay deshabilitado.

### Candidato a relay

Nodo con capacidad para habilitar la característica Relay.

### Relay dedicado

Dispositivo instalado específicamente para retransmitir tráfico. Tiene preferencia durante la selección.

### Vecino directo

Nodo que recibe un mensaje enviado con `TTL=0`. El mensaje no fue retransmitido por ningún relay.

### Anillo

Conjunto de nodos con igual distancia mínima en saltos respecto del gateway.

### Epoch

Identificador de una ejecución completa del proceso de descubrimiento.

### Probe

Mensaje local usado para medir recepción, RSSI y PDR.

### PDR

Packet Delivery Ratio:

    PDR = probes_recibidos / probes_transmitidos

### Grafo topológico

Representación donde cada nodo Bluetooth Mesh es un vértice y cada enlace bidireccional válido es una arista.

---

## 6. Direcciones y modelos

### 6.1 Direcciones de grupo sugeridas

|       Dirección | Nombre              | Uso                                 |
| --------------: | ------------------- | ----------------------------------- |
|        `0xC100` | `DISCOVERY_CONTROL` | Comandos globales de descubrimiento |
|        `0xC101` | `NEIGHBOR_PROBE`    | Probes locales con TTL 0            |
|        `0xC102` | `DISCOVERY_STATUS`  | Estados y confirmaciones            |
|        `0xC103` | `RELAY_CONTROL`     | Control lógico de relays            |
| Unicast gateway | `GATEWAY_ADDRESS`   | Reportes de tablas y resultados     |

### 6.2 Modelo de aplicación

Se define un Vendor Model común para gateway y nodos.

El modelo debe procesar:

- Inicio de descubrimiento.
- Asignación de slots.
- Probes locales.
- Reportes de vecinos.
- ACK de reportes.
- Pruebas de conectividad.
- Resultado de asignación.
- Solicitudes de redescubrimiento.

La habilitación real de Relay debe realizarse mediante los modelos estándar:

- Configuration Client en el gateway.
- Configuration Server en cada nodo.

---

## 7. Estados iniciales

### 7.1 Gateway

Al iniciar:

- Relay habilitado.
- Configuration Client disponible.
- Suscrito a los grupos de control.
- Tabla de nodos aprovisionados cargada.
- Epoch restaurada desde almacenamiento persistente.
- Configuración anterior de relays cargada.

### 7.2 Nodo candidato

Durante el primer bootstrap:

- Relay habilitado temporalmente.
- Network Transmit y Relay Retransmit configurados de forma conservadora.
- Suscrito a los grupos de descubrimiento.
- Tabla temporal de vecinos vacía.

### 7.3 Nodo común

Puede comenzar:

- Con Relay habilitado durante el primer descubrimiento, o
- Con Relay deshabilitado si se garantiza que no es necesario para alcanzar otros nodos.

La opción más segura durante la primera ejecución es habilitar Relay en todos los candidatos y deshabilitarlo después de reconstruir la topología.

---

## 8. Máquina de estados global

El proceso completo utiliza las siguientes fases:

1. `IDLE`
2. `DISCOVERY_ENTER`
3. `WAIT_READY`
4. `RING_DISCOVERY`
5. `LOCAL_DISCOVERY`
6. `COLLECT_REPORTS`
7. `BUILD_GRAPH`
8. `CALCULATE_RINGS`
9. `SELECT_RELAYS`
10. `ENABLE_NEW_RELAYS`
11. `VERIFY_NEW_RELAYS`
12. `DISABLE_OLD_RELAYS`
13. `VERIFY_NETWORK`
14. `COMMIT`
15. `ROLLBACK`
16. `COMPLETE`

Flujo principal:

    IDLE
      -> DISCOVERY_ENTER
      -> WAIT_READY
      -> RING_DISCOVERY
      -> LOCAL_DISCOVERY
      -> COLLECT_REPORTS
      -> BUILD_GRAPH
      -> CALCULATE_RINGS
      -> SELECT_RELAYS
      -> ENABLE_NEW_RELAYS
      -> VERIFY_NEW_RELAYS
      -> DISABLE_OLD_RELAYS
      -> VERIFY_NETWORK
          -> COMMIT
          -> COMPLETE

Si falla una verificación:

    VERIFY_NETWORK
      -> ROLLBACK
      -> SELECT_RELAYS
      -> ENABLE_NEW_RELAYS

---

## 9. Epoch de descubrimiento

Cada ejecución debe tener una `epoch` distinta.

```c
typedef uint16_t discovery_epoch_t;
```

Reglas:

1. El gateway incrementa la epoch antes de iniciar.
2. Epoch cero queda reservada como inválida.
3. Todo nodo elimina sus mediciones temporales al recibir una epoch nueva.
4. Los mensajes con epoch antigua deben ignorarse.
5. Los reportes persistentes deben asociarse a su epoch.
6. Un nodo no debe mezclar mediciones de distintas epochs.

Comparación considerando wraparound:

```c
static bool epoch_is_newer(uint16_t candidate, uint16_t current)
{
    return (int16_t)(candidate - current) > 0;
}
```

---

## 10. Mensajes del protocolo

Todos los campos multibyte deben serializarse explícitamente en little-endian.

### 10.1 Encabezado común

```c
typedef struct
{
    uint8_t protocol_version;
    uint8_t message_type;
    uint16_t epoch;
    uint16_t sequence;
} discovery_header_t;
```

### 10.2 Tipos de mensajes

```c
typedef enum
{
    MSG_DISCOVERY_ENTER       = 0x01,
    MSG_DISCOVERY_READY       = 0x02,
    MSG_RING_PROBE            = 0x03,
    MSG_RING_RESPONSE         = 0x04,
    MSG_SLOT_ASSIGN           = 0x05,
    MSG_NEIGHBOR_PROBE        = 0x06,
    MSG_PROBE_PHASE_COMPLETE  = 0x07,
    MSG_REPORT_REQUEST        = 0x08,
    MSG_NEIGHBOR_REPORT       = 0x09,
    MSG_REPORT_ACK            = 0x0A,
    MSG_RELAY_PLAN            = 0x0B,
    MSG_VERIFY_REQUEST        = 0x0C,
    MSG_VERIFY_RESPONSE       = 0x0D,
    MSG_DISCOVERY_COMMIT      = 0x0E,
    MSG_DISCOVERY_ABORT       = 0x0F
} discovery_message_type_t;
```

---

## 11. Etapa 1: entrada en modo descubrimiento

El gateway publica `DISCOVERY_ENTER` al grupo de control.

```c
typedef struct
{
    discovery_header_t header;

    uint32_t start_delay_ms;
    uint16_t expected_nodes;
    uint16_t slot_duration_ms;
    uint8_t probes_per_node;
    uint8_t maximum_ttl;
} discovery_enter_t;
```

Al recibirlo, cada nodo:

1. Valida la versión.
2. Compara la epoch.
3. Pausa publicaciones periódicas de datos.
4. Mantiene recepción Bluetooth Mesh activa.
5. Limpia su tabla temporal de vecinos.
6. Cambia a `DISCOVERY_PREPARING`.
7. Envía `DISCOVERY_READY`.

```c
typedef struct
{
    discovery_header_t header;

    uint16_t node_address;
    uint8_t relay_supported;
    uint8_t current_relay_state;
    uint8_t node_role;
} discovery_ready_t;
```

Roles sugeridos:

```c
typedef enum
{
    NODE_ROLE_COMMON = 0,
    NODE_ROLE_RELAY_CANDIDATE,
    NODE_ROLE_DEDICATED_REPEATER,
    NODE_ROLE_GATEWAY
} node_role_t;
```

El gateway mantiene un bitmap de nodos preparados.

Si faltan nodos:

- Reintenta `DISCOVERY_ENTER`.
- Envía el comando unicast a los faltantes.
- Marca como ausentes los que excedan el límite de intentos.
- No deshabilita relays que puedan ser necesarios para alcanzar nodos ausentes.

---

## 12. Etapa 2: descubrimiento de anillos con TTL creciente

Esta fase mide la distancia mínima observada desde el gateway.

### 12.1 Procedimiento

El gateway transmite `RING_PROBE` comenzando con:

```
TTL = 1
```

Luego incrementa:

```
TTL = 2
TTL = 3
...
TTL = MAX_TTL
```

Cada nodo responde solamente la primera vez que recibe un `RING_PROBE` para una epoch determinada.

En TTL superiores:

- No vuelve a responder.
- Continúa actuando como relay si Relay está habilitado.
- Permite que el mensaje alcance anillos posteriores.

### 12.2 Mensaje

```c
typedef struct
{
    discovery_header_t header;

    uint16_t gateway_address;
    uint8_t requested_ttl;
} ring_probe_t;
```

Respuesta:

```c
typedef struct
{
    discovery_header_t header;

    uint16_t node_address;
    uint8_t first_observed_ttl;
    int8_t received_rssi;
} ring_response_t;
```

### 12.3 Reglas del nodo

Cada nodo mantiene:

```c
typedef struct
{
    uint16_t epoch;
    bool ring_response_sent;
    uint8_t first_observed_ttl;
} node_ring_state_t;
```

Al recibir `RING_PROBE`:

1. Si la epoch es antigua, ignora el mensaje.
2. Si la epoch es nueva, limpia `ring_response_sent`.
3. Si todavía no respondió:
   - Guarda el TTL anunciado por el gateway.
   - Programa una respuesta con jitter.
   - Marca `ring_response_sent=true`.

4. Si ya respondió:
   - No responde nuevamente.

### 12.4 Interpretación

El primer TTL observado proporciona una estimación preliminar:

```
estimated_ring = first_observed_ttl
```

Esta estimación no es definitiva. Puede verse afectada por pérdidas de paquetes. El valor final del anillo se calcula posteriormente mediante BFS sobre el grafo de vecinos.

---

## 13. Etapa 3: descubrimiento local ejecutado por todos

El gateway y todos los nodos deben transmitir probes locales.

El objetivo es medir la conectividad directa entre todos los pares que se encuentren dentro de alcance de radio.

### 13.1 TTL de los probes

Los probes deben enviarse con:

```
TTL = 0
```

Esto garantiza que:

- Solo son procesados por receptores directos.
- Ningún relay los retransmite.
- La tabla representa vecindad radio real.
- No se introducen falsos vecinos debido al flooding.

### 13.2 Acceso temporal

Cada participante recibe un slot exclusivo.

Para `N` participantes:

```
total_probe_time = N × slot_duration
```

Ejemplo para 92 nodos más gateway:

```
participantes = 93
slot_duration = 300 ms
duración = 27,9 s
```

### 13.3 Asignación explícita

El gateway debe asignar slots explícitamente.

```c
typedef struct
{
    discovery_header_t header;

    uint16_t node_address;
    uint16_t slot_index;
    uint32_t phase_start_time_ms;
    uint16_t slot_duration_ms;
    uint8_t probe_count;
} slot_assign_t;
```

No debe utilizarse solamente:

```
slot = address % node_count
```

porque distintas direcciones pueden producir el mismo slot.

### 13.4 Sincronización

El gateway incluye:

- Tiempo restante hasta el comienzo.
- Índice de slot.
- Duración del slot.
- Cantidad de probes.

Cada nodo calcula:

```
slot_start =
    local_phase_start +
    slot_index × slot_duration
```

Debe incluirse una guarda:

```c
#define SLOT_GUARD_BEFORE_MS  20U
#define SLOT_GUARD_AFTER_MS   20U
```

### 13.5 Probe

```c
typedef struct
{
    discovery_header_t header;

    uint16_t source_address;
    uint8_t probe_index;
    uint8_t probe_count;
} neighbor_probe_t;
```

Durante su slot, cada participante transmite varios probes:

```text
probe 0
espera probe_interval
probe 1
espera probe_interval
...
```

Valores iniciales sugeridos:

```c
#define DISCOVERY_PROBES_PER_NODE  10U
#define PROBE_INTERVAL_MS          20U
#define DISCOVERY_SLOT_MS          300U
```

El Network Transmit del probe debe ser cero o equivalente a una sola transmisión lógica para que el PDR medido no incluya retransmisiones invisibles.

---

## 14. Medición local

Cada receptor mantiene una tabla de vecinos.

```c
#define MAX_NEIGHBORS 24U

typedef struct
{
    uint16_t address;

    uint16_t received_bitmap;
    uint8_t received_count;

    int16_t rssi_sum;
    int8_t rssi_min;
    int8_t rssi_max;

    bool valid;
} neighbor_entry_t;
```

Para más de 16 probes debe ampliarse el bitmap.

### 14.1 Eliminación de duplicados

Cada `probe_index` debe contarse una sola vez:

```c
static bool register_probe(
    neighbor_entry_t *entry,
    uint8_t probe_index,
    int8_t rssi)
{
    if (probe_index >= 16U)
    {
        return false;
    }

    const uint16_t mask =
        (uint16_t)(1U << probe_index);

    if ((entry->received_bitmap & mask) != 0U)
    {
        return false;
    }

    entry->received_bitmap |= mask;
    entry->received_count++;
    entry->rssi_sum += rssi;

    if (rssi < entry->rssi_min)
    {
        entry->rssi_min = rssi;
    }

    if (rssi > entry->rssi_max)
    {
        entry->rssi_max = rssi;
    }

    return true;
}
```

### 14.2 RSSI promedio

```c
static int8_t calculate_average_rssi(
    const neighbor_entry_t *entry)
{
    if (entry->received_count == 0U)
    {
        return INT8_MIN;
    }

    return (int8_t)(
        entry->rssi_sum /
        (int16_t)entry->received_count);
}
```

### 14.3 PDR

```c
static uint16_t calculate_pdr_per_mille(
    const neighbor_entry_t *entry,
    uint8_t expected_probes)
{
    if (expected_probes == 0U)
    {
        return 0U;
    }

    uint32_t result =
        ((uint32_t)entry->received_count * 1000U) /
        expected_probes;

    if (result > 1000U)
    {
        result = 1000U;
    }

    return (uint16_t)result;
}
```

Ejemplos:

| Recibidos | Enviados |  PDR |
| --------: | -------: | ---: |
|        10 |       10 | 1000 |
|         9 |       10 |  900 |
|         8 |       10 |  800 |
|         5 |       10 |  500 |

---

## 15. Tabla local del gateway

El gateway también participa en el descubrimiento local.

Esto permite determinar qué nodos pertenecen realmente al anillo 1.

La tabla del gateway tiene el mismo formato que la tabla de cualquier otro nodo:

```c
typedef struct
{
    uint16_t owner_address;
    uint16_t epoch;

    neighbor_entry_t entries[MAX_NEIGHBORS];
    uint8_t entry_count;
} neighbor_table_t;
```

Una conexión directa con el gateway solamente es válida si:

- El gateway recibió probes del nodo.
- El nodo recibió probes del gateway.
- Ambos PDR superan el umbral.
- Ambos RSSI superan el umbral.

---

## 16. Etapa 4: reporte de tablas al gateway

Después de finalizar todos los slots, el gateway envía `REPORT_REQUEST`.

Los nodos no deben reportar simultáneamente. Se usa una segunda planificación por slots.

### 16.1 Contenido del reporte

```c
typedef struct
{
    uint16_t neighbor_address;
    uint16_t pdr_per_mille;

    int8_t average_rssi;
    int8_t minimum_rssi;
    int8_t maximum_rssi;

    uint8_t received_probes;
} neighbor_report_entry_t;
```

Encabezado:

```c
typedef struct
{
    discovery_header_t header;

    uint16_t reporter_address;
    uint8_t fragment_index;
    uint8_t fragment_count;
    uint8_t entry_count;
} neighbor_report_header_t;
```

### 16.2 Segmentación de aplicación

Una tabla completa puede exceder el tamaño de un mensaje no segmentado de Bluetooth Mesh.

Por ello debe dividirse en fragmentos de aplicación pequeños.

Cada fragmento contiene:

- Epoch.
- Reporter.
- Índice de fragmento.
- Cantidad total.
- Cantidad de entradas.
- Entradas.

El gateway mantiene un bitmap:

```c
typedef struct
{
    uint16_t reporter_address;
    uint16_t epoch;

    uint32_t received_fragments;
    uint8_t expected_fragments;

    bool complete;
} report_reassembly_t;
```

### 16.3 Confirmación

El gateway responde con:

```c
typedef struct
{
    discovery_header_t header;

    uint16_t reporter_address;
    uint32_t received_fragment_bitmap;
    uint8_t report_complete;
} report_ack_t;
```

El nodo retransmite solamente los fragmentos faltantes.

### 16.4 TTL del reporte

Los reportes deben utilizar un TTL suficiente:

```
report_ttl = estimated_ring + ttl_margin
```

Valor inicial:

```c
#define REPORT_TTL_MARGIN 2U
```

Si el anillo todavía no es fiable:

```
report_ttl = configured_maximum_ttl
```

---

## 17. Construcción del grafo en el gateway

El gateway crea una matriz dirigida.

```c
#define MAX_NODES 128U

typedef struct
{
    bool measured;
    uint16_t pdr_per_mille;

    int8_t average_rssi;
    int8_t minimum_rssi;
    int8_t maximum_rssi;
} directed_link_t;

typedef struct
{
    uint16_t address;
    uint8_t role;

    bool relay_capable;
    bool currently_relay;
    bool selected_relay;

    uint8_t preliminary_ring;
    uint8_t calculated_ring;
} topology_node_t;

typedef struct
{
    topology_node_t nodes[MAX_NODES];
    directed_link_t links[MAX_NODES][MAX_NODES];

    size_t node_count;
} topology_t;
```

Si `A` reporta haber recibido a `B`, se registra:

```
links[B][A]
```

porque representa la dirección:

```
B transmite -> A recibe
```

---

## 18. Validación de enlaces

Un enlace principal debe ser bidireccional.

Para el enlace entre `A` y `B` deben existir:

```
A -> B
B -> A
```

Condiciones iniciales sugeridas:

```c
#define MIN_LINK_PDR_PER_MILLE 800U
#define MIN_LINK_RSSI_DBM      (-90)
```

```c
static bool link_is_valid(
    const topology_t *topology,
    size_t a,
    size_t b)
{
    const directed_link_t *ab =
        &topology->links[a][b];

    const directed_link_t *ba =
        &topology->links[b][a];

    return
        ab->measured &&
        ba->measured &&
        ab->pdr_per_mille >= MIN_LINK_PDR_PER_MILLE &&
        ba->pdr_per_mille >= MIN_LINK_PDR_PER_MILLE &&
        ab->average_rssi >= MIN_LINK_RSSI_DBM &&
        ba->average_rssi >= MIN_LINK_RSSI_DBM;
}
```

### 18.1 Calidad combinada

Puede definirse:

```
link_pdr = min(PDR_AB, PDR_BA)
link_rssi = min(RSSI_AB, RSSI_BA)
```

Se usa el peor sentido porque el enlace debe transportar solicitudes y respuestas.

### 18.2 Enlaces marginales

Un enlace marginal puede conservarse como respaldo:

```c
#define MARGINAL_PDR_PER_MILLE 600U
#define MARGINAL_RSSI_DBM      (-95)
```

No debe formar el camino principal salvo que no exista alternativa.

---

## 19. Cálculo de anillos mediante BFS

Después de construir el grafo, el gateway calcula la distancia mínima desde sí mismo.

### 19.1 Inicialización

```text
gateway.ring = 0
todos_los_demás.ring = desconocido
```

### 19.2 BFS

```c
static bool topology_calculate_rings(
    topology_t *topology,
    size_t gateway_index)
{
    size_t queue[MAX_NODES];
    size_t read_position = 0U;
    size_t write_position = 0U;

    for (size_t i = 0U; i < topology->node_count; ++i)
    {
        topology->nodes[i].calculated_ring = UINT8_MAX;
    }

    topology->nodes[gateway_index].calculated_ring = 0U;
    queue[write_position++] = gateway_index;

    while (read_position < write_position)
    {
        const size_t current = queue[read_position++];
        const uint8_t current_ring =
            topology->nodes[current].calculated_ring;

        for (size_t candidate = 0U;
             candidate < topology->node_count;
             ++candidate)
        {
            if (topology->nodes[candidate].calculated_ring !=
                UINT8_MAX)
            {
                continue;
            }

            if (!link_is_valid(topology,
                               current,
                               candidate))
            {
                continue;
            }

            topology->nodes[candidate].calculated_ring =
                (uint8_t)(current_ring + 1U);

            queue[write_position++] = candidate;
        }
    }

    return write_position == topology->node_count;
}
```

### 19.3 Comparación con TTL

Para cada nodo:

```text
preliminary_ring = primer TTL observado
calculated_ring = resultado BFS
```

Si difieren:

- Se prefiere inicialmente `calculated_ring`.
- Se marca el nodo para verificación.
- Si la diferencia es mayor que uno, se repite parcialmente el descubrimiento.

---

## 20. Selección greedy de relays

La selección no debe escoger obligatoriamente dos nodos por anillo. Debe seleccionar el mínimo número de relays capaz de proporcionar la cobertura requerida.

### 20.1 Requisitos

Para cada nodo que no está en alcance directo del gateway:

```
relay_coverage >= REQUIRED_COVERAGE
```

Valor sugerido:

```c
#define REQUIRED_COVERAGE 2U
```

Si la topología no permite dos:

- Se acepta uno temporalmente.
- Se marca el nodo como `DEGRADED`.
- Se genera una advertencia.

### 20.2 Candidato válido

Un nodo puede ser relay si:

1. Tiene `relay_capable=true`.
2. Tiene al menos un enlace válido hacia:
   - El anillo anterior, o
   - Un relay ya conectado al gateway.

3. Tiene al menos un enlace útil hacia:
   - Su mismo anillo, o
   - El anillo siguiente.

4. No se encuentra marcado como inestable.
5. No tiene restricciones de energía.

### 20.3 Puntuación

```text
score =
    role_bonus
  + backward_links × 50
  + forward_uncovered_nodes × 100
  + same_ring_uncovered_nodes × 30
  + average_link_pdr / 10
  + normalized_rssi × 2
  - overlap_penalty
  - instability_penalty
```

Bonificación por rol:

| Rol                       | Bonificación |
| ------------------------- | -----------: |
| Repetidor dedicado        |          300 |
| Nodo candidato central    |          150 |
| Nodo común                |            0 |
| Nodo con batería limitada |         -500 |

### 20.4 Cobertura marginal

La métrica principal greedy es cuántos nodos todavía insuficientemente cubiertos puede cubrir el candidato.

```c
static uint16_t count_new_coverage(
    const topology_t *topology,
    const uint8_t coverage[MAX_NODES],
    size_t candidate)
{
    uint16_t result = 0U;

    for (size_t node = 0U;
         node < topology->node_count;
         ++node)
    {
        if (coverage[node] >= REQUIRED_COVERAGE)
        {
            continue;
        }

        if (link_is_valid(topology, candidate, node))
        {
            result++;
        }
    }

    return result;
}
```

### 20.5 Selección

```text
selected = gateway
coverage = 0

mientras existan nodos con cobertura insuficiente:
    evaluar todos los candidatos conectables
    elegir candidato con mejor puntuación
    marcarlo como relay
    actualizar cobertura
    repetir
```

### 20.6 Restricción de conectividad

Un candidato solamente puede seleccionarse si ya está conectado al conjunto de relays seleccionados mediante un enlace válido.

Esto garantiza que el conjunto final sea conectado.

### 20.7 Segundo relay diverso

El segundo relay que cubre un nodo debe ser, cuando sea posible:

- Distinto del primero.
- Con enlaces ascendentes diferentes.
- No depender exactamente del mismo relay padre.
- De una ubicación física o grupo diferente.

Se aplica una penalización por solapamiento:

```text
overlap_penalty =
    vecinos_compartidos_con_relay_primario × factor
```

---

## 21. Selección orientada por anillos

La selección debe avanzar desde el gateway hacia afuera:

```
anillo 0 -> anillo 1 -> anillo 2 -> ... -> anillo N
```

Para seleccionar relays del anillo `R`, se consideran:

- Enlaces hacia relays seleccionados de `R-1`.
- Cobertura de nodos de `R`.
- Cobertura de nodos de `R+1`.

No se debe seleccionar un relay lejano que no tenga conectividad demostrada con el conjunto ya seleccionado.

---

## 22. Configuración de relays

La característica Relay se configura mediante el Configuration Client Model.

Parámetros iniciales sugeridos:

```text
Relay = Enabled
Relay Retransmit Count = 1
Relay Retransmit Interval = 20 ms
```

La configuración exacta debe validarse en terreno.

Demasiadas retransmisiones aumentan:

- Colisiones.
- Consumo.
- Latencia.
- Duplicados.

### 22.1 Orden obligatorio

1. Habilitar nuevos relays seleccionados.
2. Esperar `Config Relay Status`.
3. Probar acceso a los nuevos relays.
4. Probar nodos dependientes.
5. Deshabilitar relays antiguos no seleccionados.
6. Verificar nuevamente.

Nunca se deben deshabilitar relays antiguos antes de confirmar los nuevos.

---

## 23. Aplicación progresiva del plan

El gateway mantiene dos configuraciones:

```c
typedef struct
{
    bool previous_relay[MAX_NODES];
    bool planned_relay[MAX_NODES];
    bool applied_relay[MAX_NODES];
} relay_transaction_t;
```

### 23.1 Activación

Los nuevos relays se habilitan desde cerca hacia lejos:

```
anillo 1
anillo 2
...
anillo N
```

Esto establece progresivamente la infraestructura nueva.

### 23.2 Desactivación

Los relays no seleccionados se deshabilitan desde lejos hacia cerca:

```
anillo N
anillo N-1
...
anillo 1
```

Después de cada anillo se ejecuta una verificación.

---

## 24. Verificación

### 24.1 Verificación unicast

El gateway envía `VERIFY_REQUEST` a cada nodo.

```c
typedef struct
{
    discovery_header_t header;

    uint16_t target_address;
    uint8_t attempt;
} verify_request_t;
```

Respuesta:

```c
typedef struct
{
    discovery_header_t header;

    uint16_t node_address;
    uint8_t current_relay_state;
    uint8_t calculated_ring;
} verify_response_t;
```

### 24.2 Cantidad de intentos

```c
#define VERIFY_ATTEMPTS 3U
#define MIN_VERIFY_SUCCESSES 2U
```

Un nodo se considera accesible si responde al menos dos de tres intentos.

### 24.3 Prueba de PDR

Para una verificación más fuerte:

```text
gateway envía N solicitudes
nodo responde N veces
gateway calcula PDR de extremo a extremo
```

Umbrales:

|       PDR | Estado     |
| --------: | ---------- |
|  `>= 900` | `HEALTHY`  |
| `750–899` | `DEGRADED` |
|   `< 750` | `FAILED`   |

### 24.4 Verificación de toda la red

La configuración solamente se confirma si:

- Todos los nodos críticos están `HEALTHY`.
- No existen nodos `FAILED`.
- Los nodos `DEGRADED` cumplen la política permitida.
- Todos los relays seleccionados responden.
- Todos reportan el estado Relay esperado.

---

## 25. Rollback

Si una verificación falla:

1. Detener nuevas desactivaciones.
2. Reactivar los relays deshabilitados en la última operación.
3. Verificar acceso.
4. Marcar los enlaces relacionados como sospechosos.
5. Penalizar los candidatos que causaron el fallo.
6. Seleccionar un relay adicional.
7. Reaplicar el plan.
8. Repetir la verificación.

La configuración anterior solamente puede eliminarse después de `DISCOVERY_COMMIT`.

---

## 26. Commit

Cuando la red supera la verificación, el gateway publica:

```c
typedef struct
{
    discovery_header_t header;

    uint16_t relay_count;
    uint8_t maximum_ring;
} discovery_commit_t;
```

Cada nodo:

1. Guarda la epoch activa.
2. Guarda su estado Relay.
3. Elimina tablas temporales.
4. Reanuda el tráfico normal.
5. Cambia a `NORMAL_OPERATION`.

El gateway persiste:

- Grafo.
- Anillos.
- Relays seleccionados.
- Métricas.
- Configuración anterior.
- Fecha y causa del descubrimiento.

---

## 27. Persistencia

Cada nodo debe persistir como mínimo:

```c
typedef struct
{
    uint16_t last_committed_epoch;
    uint8_t committed_relay_state;
    uint8_t last_known_ring;
} persistent_node_state_t;
```

El gateway debe persistir:

```c
typedef struct
{
    uint16_t epoch;
    uint16_t node_address;

    uint8_t ring;
    uint8_t relay_selected;

    uint16_t last_pdr;
    int8_t last_rssi;

    uint32_t last_seen_timestamp;
} persistent_topology_entry_t;
```

Las tablas de probes sin confirmar no deben reemplazar la topología persistente.

---

## 28. Redescubrimiento

El descubrimiento completo debe ejecutarse:

- En la primera inicialización.
- Bajo comando del gateway.
- Después de añadir o eliminar nodos.
- Cuando desaparece un relay.
- Cuando varios nodos pasan a estado degradado.
- Cuando el PDR cae persistentemente.
- Después de cambios físicos importantes.

No debe ejecutarse en cada período de datos.

### 28.1 Descubrimiento parcial

Si falla un relay:

1. Mantener la configuración existente.
2. Activar candidatos de respaldo cercanos.
3. Solicitar probes solamente en:
   - Anillo del relay fallido.
   - Anillo anterior.
   - Anillo siguiente.

4. Actualizar el subgrafo.
5. Seleccionar reemplazo.
6. Verificar.
7. Confirmar.

---

## 29. Recuperación autónoma

Cada nodo relay puede mantener una lista de candidatos de respaldo calculada por el gateway.

```c
typedef struct
{
    uint16_t primary_relay;
    uint16_t backup_relays[2];
    uint8_t backup_count;
} relay_backup_plan_t;
```

Sin embargo, la habilitación automática distribuida debe evitarse si varios nodos pueden activarse simultáneamente.

La autoridad principal permanece en el gateway.

Si el gateway queda temporalmente inaccesible:

- Los relays activos permanecen activos.
- Los nodos no cambian Relay autónomamente.
- Se conserva la última configuración confirmada.
- No se aplica una configuración parcial no confirmada.

---

## 30. Control de saturación

Durante descubrimiento:

- Solo un nodo transmite probes por slot.
- Los probes usan TTL 0.
- Los reportes usan slots separados.
- Cada fragmento se confirma.
- Las retransmisiones usan backoff.
- El tráfico de aplicación queda pausado o reducido.

Backoff sugerido:

```c
delay =
    base_delay +
    random(0, jitter) +
    retry_count × retry_step
```

Ejemplo:

```c
#define REPORT_RETRY_BASE_MS  200U
#define REPORT_RETRY_JITTER_MS 150U
#define REPORT_RETRY_STEP_MS  300U
#define MAX_REPORT_RETRIES     4U
```

---

## 31. Detección de duplicados

La clave de duplicado de aplicación debe incluir:

```c
typedef struct
{
    uint8_t message_type;
    uint16_t source;
    uint16_t epoch;
    uint16_t sequence;
    uint8_t fragment_index;
} duplicate_key_t;
```

Aunque Bluetooth Mesh posee mecanismos internos de caché, la aplicación debe proteger:

- Fragmentos repetidos.
- ACK repetidos.
- Reintentos tardíos.
- Mensajes pertenecientes a epochs anteriores.

---

## 32. Temporización sugerida para 92 nodos

Configuración inicial:

```text
Nodos:                         92
Gateway participante:          1
Participantes totales:         93
Probes por participante:       10
Slot de probes:                300 ms
Tiempo de probes:              27,9 s
Slot de reporte:               500–1000 ms
Fragmentos por tabla:          variable
Intentos de verificación:      3
```

Duración aproximada:

| Etapa               | Tiempo estimado |
| ------------------- | --------------: |
| Entrada y READY     |           3–8 s |
| TTL creciente       |          5–20 s |
| Probes locales      |          27,9 s |
| Reportes            |         20–90 s |
| Cálculo             |            <1 s |
| Configuración Relay |         10–60 s |
| Verificación        |         20–90 s |

El descubrimiento completo puede tardar varios minutos y debe considerarse mantenimiento de red, no parte del ciclo normal de adquisición.

---

## 33. Parámetros configurables

```c
typedef struct
{
    uint8_t maximum_ttl;

    uint8_t probes_per_node;
    uint16_t probe_slot_ms;
    uint16_t probe_interval_ms;

    uint16_t minimum_pdr_per_mille;
    int8_t minimum_rssi_dbm;

    uint8_t required_relay_coverage;
    uint8_t maximum_relays_per_ring;

    uint8_t verify_attempts;
    uint8_t minimum_verify_successes;

    uint8_t maximum_report_retries;
    uint16_t report_timeout_ms;

    uint8_t relay_retransmit_count;
    uint16_t relay_retransmit_interval_ms;
} discovery_configuration_t;
```

Valores iniciales sugeridos:

```c
static const discovery_configuration_t default_configuration = {
    .maximum_ttl = 16U,

    .probes_per_node = 10U,
    .probe_slot_ms = 300U,
    .probe_interval_ms = 20U,

    .minimum_pdr_per_mille = 800U,
    .minimum_rssi_dbm = -90,

    .required_relay_coverage = 2U,
    .maximum_relays_per_ring = 3U,

    .verify_attempts = 3U,
    .minimum_verify_successes = 2U,

    .maximum_report_retries = 4U,
    .report_timeout_ms = 3000U,

    .relay_retransmit_count = 1U,
    .relay_retransmit_interval_ms = 20U
};
```

---

## 34. Criterios de éxito

El algoritmo se considera exitoso cuando:

1. Todos los nodos esperados participaron o fueron clasificados explícitamente como ausentes.
2. El gateway recibió una tabla completa de cada nodo participante.
3. Todos los enlaces principales son bidireccionales.
4. BFS pudo conectar todos los nodos presentes al gateway.
5. Todos los nodos tienen anillo asignado.
6. El conjunto de relays es conectado.
7. Cada nodo posee la cobertura de relay requerida cuando la topología lo permite.
8. Todos los nuevos relays confirmaron su configuración.
9. Todos los nodos superaron la verificación final.
10. La configuración fue persistida mediante commit.

---

## 35. Condiciones de fallo

El algoritmo debe abortar o entrar en modo degradado si:

- El gateway no puede alcanzar una parte de la red durante bootstrap.
- Faltan tablas de vecinos críticas.
- El grafo contiene componentes desconectados.
- No existen candidatos relay suficientes.
- Un relay no acepta la configuración.
- La verificación falla incluso después de añadir relays.
- El rollback no restaura la conectividad.
- Se detecta un cambio de epoch durante la ejecución.

En caso de aborto:

- Se conserva la última configuración confirmada.
- Se reactivan los relays de la configuración anterior.
- Se publica `DISCOVERY_ABORT`.
- Se reanuda el tráfico normal en modo degradado.
- Se registra la causa.

---

## 36. Consideraciones específicas para la correa transportadora

Para una topología formada por grupos de tres nodos:

- Nodo central: candidato relay preferente.
- Nodos superior e inferior: nodos comunes.
- Repetidor dedicado: máxima prioridad.
- Gateway ubicado cerca del centro: BFS crecerá hacia ambos extremos.

No debe asumirse que existe una única secuencia lineal de anillos. Desde un gateway central habrá al menos dos ramas:

```
extremo izquierdo <- gateway -> extremo derecho
```

Por eso el algoritmo greedy debe considerar cobertura por ramas y no seleccionar solamente dos nodos globales por cada anillo.

En un mismo anillo podrían necesitarse:

- Dos relays para la rama izquierda.
- Dos relays para la rama derecha.

El límite `maximum_relays_per_ring` debe aplicarse con cuidado. Es preferible limitar relays por región o componente del anillo, no solamente por número de TTL.

---

## 37. Resultado final esperado

Después de finalizar:

- El gateway conserva Relay habilitado.
- Los repetidores dedicados necesarios conservan Relay habilitado.
- Solo los nodos seleccionados actúan como relays.
- Los demás nodos mantienen Relay deshabilitado.
- Todos los nodos siguen enviando sus datos mediante managed flooding.
- El TTL normal se ajusta según el máximo anillo requerido.
- La cantidad de flooding disminuye.
- Se mantienen caminos redundantes cuando la topología lo permite.

La topología lógica resultante se aproxima a un conjunto dominante conectado redundante, pero continúa utilizando el mecanismo estándar de managed flooding de Bluetooth Mesh.

---

## 38. Resumen del algoritmo

```text
1. Gateway incrementa epoch.
2. Gateway ordena pausar tráfico normal.
3. Todos los nodos confirman DISCOVERY_READY.
4. Gateway ejecuta pruebas con TTL creciente.
5. Cada nodo responde una sola vez por epoch.
6. Gateway asigna un slot de probe a cada nodo y a sí mismo.
7. Cada participante transmite probes con TTL 0.
8. Todos construyen tablas locales de vecinos.
9. Gateway asigna slots para reportar.
10. Todos envían sus tablas fragmentadas al gateway.
11. Gateway confirma cada reporte.
12. Gateway construye el grafo dirigido.
13. Gateway elimina enlaces débiles o unidireccionales.
14. Gateway ejecuta BFS y calcula los anillos.
15. Gateway compara BFS con el TTL observado.
16. Gateway ejecuta selección greedy de relays.
17. Gateway habilita primero los nuevos relays.
18. Gateway verifica los nuevos caminos.
19. Gateway deshabilita relays innecesarios.
20. Gateway verifica todos los nodos.
21. Si falla, restaura relays y añade candidatos.
22. Si funciona, persiste la configuración.
23. Gateway publica DISCOVERY_COMMIT.
24. Todos reanudan la operación normal.
```

## 39. Supervisión local y reemplazo automático de relays

Cada relay seleccionado debe tener al menos un nodo vecino designado como relay de respaldo local.

El respaldo supervisa directamente al relay mediante beacons de aplicación enviados con:

    TTL = 0

Si el respaldo deja de recibir los beacons durante un intervalo definido, habilita temporalmente su propia característica Relay para mantener la conectividad.

Cuando el relay primario reaparece y permanece estable, el respaldo deshabilita Relay y retorna a operación normal.

Este mecanismo proporciona recuperación local sin esperar que el gateway detecte y repare la topología completa.

---

## 40. Roles de redundancia

Para cada relay seleccionado se definen:

- Un relay primario.
- Uno o dos vecinos de respaldo.
- Un orden de prioridad entre los respaldos.
- Un lease o generación de configuración.
- Un período de beacon.
- Un límite de beacons perdidos.
- Un tiempo mínimo como relay antes de volver a normal.

```c
#define MAX_RELAY_BACKUPS 2U

typedef struct
{
    uint16_t primary_relay;
    uint16_t backup_nodes[MAX_RELAY_BACKUPS];
    uint8_t backup_count;

    uint16_t configuration_generation;

    uint16_t beacon_period_ms;
    uint8_t missed_beacon_threshold;

    uint16_t takeover_delay_ms;
    uint16_t recovery_stable_ms;
    uint16_t minimum_relay_active_ms;
} relay_watchdog_plan_t;
```

El gateway calcula y distribuye este plan durante la etapa de selección de relays.

---

## 41. Selección del respaldo local

El respaldo de un relay debe cumplir:

1. Ser vecino directo del relay primario.
2. Haber recibido probes del relay primario con `TTL=0`.
3. Haber sido detectado también por el relay primario.
4. Tener un enlace bidireccional válido.
5. Tener capacidad de Relay.
6. Poder cubrir al menos parte de los mismos vecinos del primario.
7. Tener conectividad hacia el anillo anterior.
8. Preferiblemente tener conectividad hacia el anillo siguiente.
9. No ser respaldo simultáneo de demasiados relays críticos.
10. Tener suficiente energía para asumir temporalmente la función.

No basta con que el respaldo escuche al relay. También debe ser capaz de sustituir su función dentro del grafo.

### 41.1 Puntuación del respaldo

```text
backup_score =
    shared_covered_nodes × 100
  + backward_links × 80
  + forward_links × 80
  + link_pdr_to_primary / 10
  + normalized_rssi_to_primary × 2
  + role_bonus
  - active_backup_load × 50
  - topology_overlap_penalty
```

La variable `shared_covered_nodes` representa cuántos vecinos del relay primario también pueden ser alcanzados directamente por el respaldo.

---

## 42. Beacon local del relay

Cada relay activo transmite periódicamente un beacon de supervisión.

```c
typedef struct
{
    discovery_header_t header;

    uint16_t relay_address;
    uint16_t configuration_generation;

    uint16_t beacon_sequence;
    uint8_t relay_state;
    uint8_t relay_priority;
} relay_alive_beacon_t;
```

El beacon se publica con:

```text
Destino: grupo local de supervisión o dirección del respaldo
TTL: 0
Application retransmit: 0
Relay retransmit: no aplica, porque TTL es 0
```

El beacon no debe ser retransmitido por otros relays.

### 42.1 Período sugerido

```c
#define RELAY_BEACON_PERIOD_MS          2000U
#define RELAY_MISSED_BEACON_THRESHOLD   3U
#define RELAY_RECOVERY_STABLE_MS        10000U
#define RELAY_MINIMUM_ACTIVE_MS         15000U
```

Con estos valores:

```text
Detección nominal de fallo:
    aproximadamente 6 segundos

Confirmación de recuperación:
    10 segundos de recepción estable

Tiempo mínimo del respaldo como relay:
    15 segundos
```

Aunque el respaldo debe reaccionar rápidamente, no debe activarse por la pérdida de un solo beacon. Bluetooth Mesh puede perder mensajes debido a colisiones o interferencias.

---

## 43. Estado local del supervisor

```c
typedef enum
{
    RELAY_BACKUP_NORMAL = 0,
    RELAY_BACKUP_SUSPECTING,
    RELAY_BACKUP_WAIT_TAKEOVER,
    RELAY_BACKUP_ACTIVE,
    RELAY_BACKUP_RECOVERY_MONITORING,
    RELAY_BACKUP_RETURNING_NORMAL
} relay_backup_state_t;

typedef struct
{
    uint16_t local_address;
    uint16_t primary_relay;

    uint16_t expected_generation;
    uint16_t last_beacon_sequence;

    uint32_t last_beacon_time_ms;
    uint32_t state_enter_time_ms;
    uint32_t relay_activation_time_ms;
    uint32_t recovery_start_time_ms;

    uint16_t beacon_period_ms;
    uint16_t takeover_delay_ms;
    uint16_t recovery_stable_ms;
    uint16_t minimum_relay_active_ms;

    uint8_t missed_threshold;
    uint8_t backup_priority;

    bool local_relay_enabled;
    bool received_beacon;
    relay_backup_state_t state;
} relay_backup_monitor_t;
```

---

## 44. Recepción del beacon

Al recibir un beacon válido, el respaldo:

1. Verifica la epoch.
2. Verifica `configuration_generation`.
3. Verifica que `relay_address` sea su relay primario asignado.
4. Verifica que el beacon haya llegado directamente con `TTL=0`.
5. Actualiza `last_beacon_time_ms`.
6. Registra la secuencia.
7. Descarta duplicados.
8. Actualiza su máquina de estados.

```c
static void relay_backup_on_beacon(
    relay_backup_monitor_t *monitor,
    uint16_t source_address,
    uint16_t generation,
    uint16_t sequence,
    uint32_t now_ms)
{
    if (source_address != monitor->primary_relay)
    {
        return;
    }

    if (generation != monitor->expected_generation)
    {
        return;
    }

    if ((uint16_t)(sequence -
                   monitor->last_beacon_sequence) == 0U)
    {
        return;
    }

    monitor->last_beacon_sequence = sequence;
    monitor->last_beacon_time_ms = now_ms;
    monitor->received_beacon = true;

    if (monitor->state == RELAY_BACKUP_SUSPECTING ||
        monitor->state == RELAY_BACKUP_WAIT_TAKEOVER)
    {
        monitor->state = RELAY_BACKUP_NORMAL;
        monitor->state_enter_time_ms = now_ms;
    }
}
```

---

## 45. Detección de ausencia

El tiempo límite se calcula como:

```text
beacon_timeout =
    beacon_period × missed_beacon_threshold
```

```c
static bool relay_beacon_timed_out(
    const relay_backup_monitor_t *monitor,
    uint32_t now_ms)
{
    const uint32_t timeout_ms =
        (uint32_t)monitor->beacon_period_ms *
        monitor->missed_threshold;

    return (int32_t)(
        now_ms -
        monitor->last_beacon_time_ms) >=
        (int32_t)timeout_ms;
}
```

El respaldo no debe asumir Relay inmediatamente después de perder un único beacon.

Transición:

```text
NORMAL
  -> no llega beacon dentro del timeout
SUSPECTING
  -> espera takeover_delay según prioridad
WAIT_TAKEOVER
  -> si el beacon reaparece, vuelve a NORMAL
  -> si continúa ausente, habilita Relay
ACTIVE
```

---

## 46. Evitar que varios respaldos se activen simultáneamente

Si un relay tiene dos respaldos, ambos podrían detectar la pérdida al mismo tiempo.

Para evitarlo, cada respaldo recibe una prioridad:

| Prioridad | Función             |  Retardo |
| --------: | ------------------- | -------: |
|         0 | Respaldo primario   | 0–500 ms |
|         1 | Respaldo secundario |    2–4 s |
|         2 | Respaldo terciario  |    4–8 s |

```c
static uint32_t calculate_takeover_delay_ms(
    uint8_t backup_priority,
    uint16_t local_address)
{
    const uint32_t priority_delay =
        (uint32_t)backup_priority * 3000U;

    const uint32_t deterministic_jitter =
        ((uint32_t)local_address * 37U) % 500U;

    return priority_delay + deterministic_jitter;
}
```

El respaldo secundario debe cancelar su activación si, durante su espera, recibe un beacon de reemplazo emitido por el respaldo primario.

---

## 47. Beacon del relay sustituto

Cuando un respaldo habilita Relay, comienza a transmitir:

```c
typedef struct
{
    discovery_header_t header;

    uint16_t failed_relay;
    uint16_t replacement_relay;

    uint16_t configuration_generation;
    uint16_t beacon_sequence;

    uint8_t backup_priority;
} relay_takeover_beacon_t;
```

Configuración:

```text
TTL: 0
Período: igual al beacon normal
```

Los demás respaldos del mismo relay fallido escuchan este beacon.

Si un respaldo de menor prioridad detecta un sustituto válido, permanece en estado normal.

---

## 48. Activación local de Relay

Cuando vence el timeout y el retardo de prioridad:

1. El respaldo habilita su característica Relay.
2. Conserva su configuración de retransmisión preasignada.
3. Registra el momento de activación.
4. Comienza a emitir `RELAY_TAKEOVER_BEACON`.
5. Informa al gateway.
6. Permanece activo durante un tiempo mínimo.

```c
static void activate_backup_relay(
    relay_backup_monitor_t *monitor,
    uint32_t now_ms)
{
    if (monitor->local_relay_enabled)
    {
        return;
    }

    /*
     * La implementación concreta debe modificar localmente el estado
     * Relay del nodo y mantenerlo sincronizado con Configuration Server.
     */
    platform_set_local_relay(true);

    monitor->local_relay_enabled = true;
    monitor->relay_activation_time_ms = now_ms;
    monitor->state = RELAY_BACKUP_ACTIVE;
    monitor->state_enter_time_ms = now_ms;

    send_relay_takeover_event(
        monitor->primary_relay,
        monitor->local_address,
        monitor->expected_generation,
        monitor->backup_priority);
}
```

La configuración local debe actualizar el mismo estado que expone el Configuration Server. No debe existir un estado Relay interno diferente del estado reportado por Bluetooth Mesh.

---

## 49. Notificación al gateway

Después de activarse, el respaldo envía:

```c
typedef struct
{
    discovery_header_t header;

    uint16_t failed_relay;
    uint16_t replacement_relay;

    uint32_t last_primary_beacon_age_ms;
    uint8_t backup_priority;
    uint8_t local_relay_state;
} relay_takeover_event_t;
```

El mensaje utiliza un TTL suficiente para alcanzar el gateway:

```text
TTL = ring + margin
```

Si el gateway no responde:

- El respaldo continúa activo.
- Reintenta la notificación con backoff.
- No deshabilita Relay únicamente por falta de ACK del gateway.

El gateway actualiza temporalmente la topología:

```text
relay primario: sospechoso o fuera de línea
respaldo: relay temporal activo
```

---

## 50. Retorno del relay primario

Cuando el respaldo activo vuelve a recibir el beacon del relay primario, no debe deshabilitarse inmediatamente.

Primero entra en:

```
RELAY_BACKUP_RECOVERY_MONITORING
```

Debe comprobar:

1. Beacons consecutivos durante `recovery_stable_ms`.
2. Epoch correcta.
3. Generación correcta.
4. Secuencias crecientes.
5. RSSI por encima del mínimo.
6. PDR de beacons por encima del umbral.
7. Tiempo mínimo como relay ya cumplido.

### 50.1 Condición de estabilidad

```c
static bool primary_is_stable(
    const relay_backup_monitor_t *monitor,
    uint32_t now_ms)
{
    const bool stable_period_complete =
        (int32_t)(
            now_ms -
            monitor->recovery_start_time_ms) >=
        (int32_t)monitor->recovery_stable_ms;

    const bool minimum_active_complete =
        (int32_t)(
            now_ms -
            monitor->relay_activation_time_ms) >=
        (int32_t)monitor->minimum_relay_active_ms;

    return stable_period_complete &&
           minimum_active_complete;
}
```

---

## 51. Retorno a modo normal

Cuando el relay primario se considera estable:

1. El respaldo informa al gateway que iniciará retorno.
2. Mantiene Relay activo durante un período de solapamiento.
3. El relay primario y el respaldo retransmiten temporalmente.
4. Finalizado el período, el respaldo deshabilita Relay.
5. El respaldo deja de enviar beacons de sustitución.
6. Retorna a `RELAY_BACKUP_NORMAL`.

```c
#define RELAY_HANDOVER_OVERLAP_MS 3000U
```

```c
static void deactivate_backup_relay(
    relay_backup_monitor_t *monitor,
    uint32_t now_ms)
{
    platform_set_local_relay(false);

    monitor->local_relay_enabled = false;
    monitor->state = RELAY_BACKUP_NORMAL;
    monitor->state_enter_time_ms = now_ms;
    monitor->recovery_start_time_ms = 0U;

    send_relay_restored_event(
        monitor->primary_relay,
        monitor->local_address,
        monitor->expected_generation);
}
```

El respaldo solamente puede deshabilitarse si el beacon recibido indica que el primario tiene Relay habilitado.

---

## 52. Histéresis

La histéresis evita oscilaciones cuando un enlace es inestable.

Se aplican cuatro tiempos diferentes:

```text
beacon_period             = período normal
failure_timeout           = detección de ausencia
recovery_stable_time      = confirmación de recuperación
minimum_relay_active_time = permanencia mínima como relay
```

Debe cumplirse:

```text
recovery_stable_time > failure_timeout
minimum_relay_active_time >= recovery_stable_time
```

Ejemplo:

```text
beacon_period             = 2 s
failure_timeout           = 6 s
recovery_stable_time      = 10 s
minimum_relay_active_time = 15 s
```

No debe implementarse:

```text
pierde un beacon -> activa Relay
recibe un beacon -> desactiva Relay
```

Ese comportamiento produciría oscilaciones, cambios continuos de topología y flooding adicional.

---

## 53. Máquina de estados del respaldo

```text
NORMAL
  |
  | timeout de beacon primario
  v
SUSPECTING
  |
  | continúa ausente
  v
WAIT_TAKEOVER
  |
  | vence retardo de prioridad
  v
ACTIVE
  |
  | vuelve beacon del primario
  v
RECOVERY_MONITORING
  |
  | primario estable y tiempo mínimo cumplido
  v
RETURNING_NORMAL
  |
  | finaliza período de solapamiento
  v
NORMAL
```

Transiciones alternativas:

```text
SUSPECTING + beacon válido
    -> NORMAL

WAIT_TAKEOVER + beacon primario válido
    -> NORMAL

WAIT_TAKEOVER + takeover beacon de respaldo superior
    -> NORMAL

RECOVERY_MONITORING + nueva pérdida
    -> ACTIVE

RETURNING_NORMAL + nueva pérdida
    -> ACTIVE
```

---

## 54. Procesamiento periódico

```c
static void relay_backup_process(
    relay_backup_monitor_t *monitor,
    uint32_t now_ms)
{
    switch (monitor->state)
    {
        case RELAY_BACKUP_NORMAL:
            if (relay_beacon_timed_out(monitor, now_ms))
            {
                monitor->state = RELAY_BACKUP_SUSPECTING;
                monitor->state_enter_time_ms = now_ms;
            }
            break;

        case RELAY_BACKUP_SUSPECTING:
            if (!relay_beacon_timed_out(monitor, now_ms))
            {
                monitor->state = RELAY_BACKUP_NORMAL;
            }
            else
            {
                monitor->takeover_delay_ms =
                    calculate_takeover_delay_ms(
                        monitor->backup_priority,
                        monitor->local_address);

                monitor->state = RELAY_BACKUP_WAIT_TAKEOVER;
                monitor->state_enter_time_ms = now_ms;
            }
            break;

        case RELAY_BACKUP_WAIT_TAKEOVER:
            if (!relay_beacon_timed_out(monitor, now_ms))
            {
                monitor->state = RELAY_BACKUP_NORMAL;
            }
            else if ((int32_t)(
                         now_ms -
                         monitor->state_enter_time_ms) >=
                     (int32_t)monitor->takeover_delay_ms)
            {
                activate_backup_relay(monitor, now_ms);
            }
            break;

        case RELAY_BACKUP_ACTIVE:
            if (!relay_beacon_timed_out(monitor, now_ms))
            {
                monitor->recovery_start_time_ms = now_ms;
                monitor->state =
                    RELAY_BACKUP_RECOVERY_MONITORING;
            }
            break;

        case RELAY_BACKUP_RECOVERY_MONITORING:
            if (relay_beacon_timed_out(monitor, now_ms))
            {
                monitor->state = RELAY_BACKUP_ACTIVE;
                monitor->recovery_start_time_ms = 0U;
            }
            else if (primary_is_stable(monitor, now_ms))
            {
                monitor->state =
                    RELAY_BACKUP_RETURNING_NORMAL;
                monitor->state_enter_time_ms = now_ms;
            }
            break;

        case RELAY_BACKUP_RETURNING_NORMAL:
            if (relay_beacon_timed_out(monitor, now_ms))
            {
                monitor->state = RELAY_BACKUP_ACTIVE;
            }
            else if ((int32_t)(
                         now_ms -
                         monitor->state_enter_time_ms) >=
                     RELAY_HANDOVER_OVERLAP_MS)
            {
                deactivate_backup_relay(monitor, now_ms);
            }
            break;

        default:
            monitor->state = RELAY_BACKUP_NORMAL;
            break;
    }
}
```

---

## 55. Relación con el algoritmo greedy

El algoritmo greedy debe seleccionar dos elementos distintos:

1. Relays activos.
2. Respaldos locales de esos relays.

Después de seleccionar un relay, el gateway busca su mejor vecino sustituto.

```text
para cada relay seleccionado:
    encontrar vecinos directos relay-capable
    eliminar candidatos sin enlace hacia atrás
    calcular cobertura compartida
    calcular puntuación de respaldo
    seleccionar respaldo primario
    seleccionar respaldo secundario, si existe
```

Un nodo de respaldo no cuenta como relay activo durante operación normal.

Sin embargo, debe incluirse en las simulaciones de fallo:

```text
eliminar relay primario del grafo
activar respaldo
comprobar conectividad desde gateway
```

El respaldo solamente es válido si esa simulación conserva la conectividad de los nodos protegidos.

---

## 56. Verificación de tolerancia a fallos

Antes de confirmar la topología, el gateway debe simular el fallo individual de cada relay.

Para cada relay `R`:

1. Eliminar temporalmente `R` del grafo.
2. Añadir su respaldo `B` al conjunto activo.
3. Ejecutar BFS.
4. Verificar que los nodos dependientes continúen conectados.
5. Verificar que no se exceda el TTL máximo.
6. Verificar cobertura suficiente.

Resultado:

```c
typedef struct
{
    uint16_t primary_relay;
    uint16_t backup_relay;

    bool connectivity_preserved;
    uint16_t affected_nodes;
    uint8_t resulting_maximum_ring;
} relay_failure_simulation_t;
```

Si el respaldo no conserva conectividad:

- Seleccionar otro respaldo.
- Añadir un relay activo adicional.
- Marcar la región como no redundante.
- No confirmar silenciosamente una falsa tolerancia a fallos.

---

## 57. Limitación del beacon TTL 0

El beacon TTL 0 verifica exclusivamente:

- Que el relay está transmitiendo.
- Que existe un enlace radio directo desde el relay hasta el respaldo.
- Que el firmware del relay continúa ejecutándose.

No garantiza por sí mismo:

- Que el relay esté retransmitiendo correctamente.
- Que tenga conectividad hacia el gateway.
- Que sus enlaces hacia el siguiente anillo funcionen.
- Que su estado interno Relay sea realmente efectivo.

Por eso el beacon debe incluir el estado Relay reportado por el firmware y el gateway debe mantener verificaciones extremo a extremo periódicas.

Opcionalmente, el relay puede incluir contadores:

```c
typedef struct
{
    uint32_t relayed_messages;
    uint32_t received_network_pdus;
    uint16_t buffer_failures;
    uint16_t uptime_seconds;
} relay_health_counters_t;
```

---

## 58. Casos especiales

### 58.1 El relay funciona, pero el enlace con su respaldo falla

El respaldo podría activarse aunque el relay siga operativo.

Esto es aceptable temporalmente porque mantiene redundancia, pero el gateway debe detectar que ambos siguen accesibles y recalcular el par de supervisión.

### 58.2 El relay reinicia

Durante el reinicio:

- El respaldo detecta ausencia.
- Activa Relay.
- El primario restaura su configuración persistente.
- Reanuda sus beacons.
- El respaldo espera estabilidad.
- El respaldo retorna a normal.

### 58.3 El gateway está desconectado

La recuperación local sigue funcionando.

Los respaldos:

- Activan Relay cuando corresponde.
- Mantienen la configuración.
- No requieren autorización inmediata del gateway.
- Informan los cambios cuando el gateway reaparece.

### 58.4 Primario y respaldo fallan

El respaldo secundario puede activarse usando un retardo mayor.

Si no existe respaldo secundario:

- La región puede quedar desconectada.
- Los nodos conservan su configuración.
- El gateway ejecuta redescubrimiento parcial cuando recupera contacto.

### 58.5 Flapping del relay primario

Si el primario aparece y desaparece repetidamente:

- El respaldo permanece activo.
- Se incrementa un contador de fallos.
- Se extiende `recovery_stable_ms`.
- El gateway marca el primario como inestable.
- El gateway puede convertir permanentemente el respaldo en primario.

---

## 59. Promoción permanente

Si un respaldo debe activarse repetidamente, el gateway puede promoverlo permanentemente.

Condición sugerida:

```text
takeovers >= 3 dentro de 30 minutos
```

Acciones:

1. Marcar el relay original como inestable.
2. Convertir el respaldo en relay seleccionado permanente.
3. Seleccionar un nuevo respaldo para el relay promovido.
4. Incrementar `configuration_generation`.
5. Distribuir el nuevo plan.
6. Ejecutar verificación.
7. Confirmar mediante commit.

---

## 60. Actualización del resumen general

El algoritmo completo queda definido como:

```text
1. Gateway incrementa epoch.
2. Todos entran en modo descubrimiento.
3. Todos confirman que están preparados.
4. Gateway ejecuta descubrimiento con TTL creciente.
5. Cada nodo responde una sola vez por epoch.
6. Gateway asigna slots de probes.
7. Gateway y todos los nodos transmiten probes TTL 0.
8. Todos construyen tablas de vecinos.
9. Todos reportan sus tablas al gateway.
10. Gateway construye el grafo bidireccional.
11. Gateway calcula los anillos mediante BFS.
12. Gateway selecciona relays mediante greedy.
13. Gateway selecciona uno o dos respaldos por relay.
14. Gateway simula el fallo individual de cada relay.
15. Gateway habilita los nuevos relays.
16. Gateway verifica conectividad.
17. Gateway deshabilita relays innecesarios.
18. Gateway distribuye planes de supervisión local.
19. Cada relay transmite un beacon periódico con TTL 0.
20. Cada respaldo supervisa directamente a su relay.
21. Si vence el timeout, el respaldo activa Relay.
22. El respaldo informa al gateway y transmite beacon de takeover.
23. Si reaparece el primario, ambos permanecen activos temporalmente.
24. Después de verificar estabilidad, el respaldo deshabilita Relay.
25. Si el primario falla repetidamente, el gateway promueve el respaldo.
26. Todos los cambios utilizan epoch y configuration_generation.
27. La configuración anterior se conserva hasta completar el commit.
```
