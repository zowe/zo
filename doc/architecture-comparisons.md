# Comparing Zowe Remote SSH (ZNP) vs z/OSMF REST API

This document helps illustrate the architectural differences between Zowe Remote SSH (SSH-based) and z/OSMF REST API approaches for mainframe access in Zowe Clients.

## High-Level Architecture

### ZNP

```mermaid
graph TB
    subgraph "Client Side"
        CLI["Zowe CLI Plugin<br/>zssh commands"]
        VSC["VS Code Extension<br/>Zowe Explorer"]
        SDK["Node.js SDK<br/>ZSshClient"]
        APP["Custom Applications"]

        CLI --> SDK
        VSC --> SDK
        APP --> SDK
    end

    subgraph "Network Layer"
        SSH["SSH Connection<br/>Port 22<br/>Encrypted Channel"]
    end

    subgraph "z/OS Server Side"
        subgraph "API Mediation Layer"
            ZOWEA["Gateway<br/>(-)<br/>-"]
        end
        subgraph "SSH Server Process"
            ZOWED["zo server<br/>(C++ I/O Server)<br/>JSON-RPC Middleware"]
            ZOWEP["python-bi<br/>(Python Service)<br/>REST/Flask Middleware"]
        end

        subgraph "Native Backend"
            ZOWEX2["zo<br/>(Executable Binary)<br/>Metal C + HLASM"]
            ZOWEP2["python-bi<br/>(Python Bindings)<br/>Backend"]
            ZOWEX["zo<br/>(C++ Modules)<br/>Metal C + HLASM"]
        end

        subgraph "z/OS System Services"
            DS["Data Sets<br/>VSAM/QSAM"]
            USS["USS Files<br/>HFS/zFS"]
            JES["JES Subsystem<br/>Jobs & Spool"]
            MVS["MVS Console<br/>System Commands"]
        end
    end

    SDK <--> SSH
    SDK <--> ZOWEA
    SSH <--> ZOWED
    ZOWEA <--> ZOWEP
    ZOWEP <--> ZOWEP2
    ZOWEP2 <--> ZOWEX
    ZOWED <--> ZOWEX2
    ZOWEX2 <--> ZOWEX
    ZOWEX --> DS
    ZOWEX --> USS
    ZOWEX --> JES
    ZOWEX --> MVS

    style SSH fill:#e1f5fe
    style ZOWED fill:#f3e5f5
    style ZOWEX fill:#e8f5e8
    style SDK fill:#fff3e0
```

### z/OSMF REST API

```mermaid
graph TB
    subgraph "Client Side"
        CLI2["Zowe CLI<br/>Standard Commands"]
        VSC2["VS Code Extension<br/>Zowe Explorer"]
        SDK2["Node.js SDK<br/>REST Client"]
        APP2["Custom Applications"]

        CLI2 --> SDK2
        VSC2 --> SDK2
        APP2 --> SDK2
    end

    subgraph "Network Layer"
        HTTPS["HTTPS Connection<br/>Port 443/10443<br/>TLS Encrypted"]
    end

    subgraph "z/OS Server Side"
        subgraph "z/OSMF Server"
            ZOSMF["z/OSMF<br/>(Java/Liberty)<br/>REST API Gateway"]
            AUTH["Authentication<br/>& Authorization"]
        end

        subgraph "z/OS System Services"
            DS2["Data Sets<br/>VSAM/QSAM"]
            USS2["USS Files<br/>HFS/zFS"]
            JES2["JES Subsystem<br/>Jobs & Spool"]
            MVS2["MVS Console<br/>System Commands"]
            TSO["TSO Address Space<br/>Interactive Commands"]
        end
    end

    SDK2 <--> HTTPS
    HTTPS <--> ZOSMF
    ZOSMF --> AUTH
    ZOSMF --> DS2
    ZOSMF --> USS2
    ZOSMF --> JES2
    ZOSMF --> MVS2
    ZOSMF --> TSO

    style HTTPS fill:#e1f5fe
    style ZOSMF fill:#fff3e0
    style AUTH fill:#ffebee
    style SDK2 fill:#fff3e0
```

## Request/Response Flow

### ZNP

```mermaid
sequenceDiagram
    participant Client as Client Application
    participant SDK as ZSshClient SDK
    participant SSH as SSH Connection
    participant IOServer as zo server (C++ Server)
    participant Backend as zo (C++ Binary)
    participant ZOS as z/OS Resources

    Client->>SDK: Request Operation<br/>(e.g., listDatasets)
    SDK->>SSH: JSON-RPC Request<br/>over SSH stdin
    SSH->>IOServer: Forward JSON Request
    IOServer->>IOServer: Parse & Route Request
    IOServer->>Backend: Execute command handler
    Backend->>ZOS: System Calls<br/>(VSAM, QSAM, etc.)
    ZOS-->>Backend: Raw Data
    Backend-->>IOServer: Formatted Response
    IOServer->>IOServer: JSON Serialization
    IOServer->>SSH: JSON Response<br/>via stdout
    SSH->>SDK: Response Data
    SDK-->>Client: Typed Response Object

    Note over SSH: Single persistent connection<br/>Multiple concurrent requests
    Note over Backend: Direct system calls<br/>Metal C performance
```

### z/OSMF REST API

```mermaid
sequenceDiagram
    participant Client as Client Application
    participant SDK as REST SDK
    participant HTTPS as HTTPS Connection
    participant ZOSMF as z/OSMF Server
    participant AUTH as Authentication
    participant TSO as TSO Address Space
    participant ZOS as z/OS Resources

    Client->>SDK: Request Operation<br/>(e.g., list data sets)
    SDK->>HTTPS: HTTP REST Request<br/>JSON payload
    HTTPS->>ZOSMF: Forward HTTP Request
    ZOSMF->>AUTH: Validate Credentials<br/>& Authorization
    AUTH-->>ZOSMF: Auth Success
    ZOSMF->>TSO: Start/Use TSO Session
    TSO->>ZOS: TSO Commands<br/>(LISTDS, LISTCAT, etc.)
    ZOS-->>TSO: Command Output
    TSO-->>ZOSMF: Text Response
    ZOSMF->>ZOSMF: Parse & Transform<br/>to JSON
    ZOSMF->>HTTPS: HTTP JSON Response
    HTTPS->>SDK: Response Data
    SDK-->>Client: Typed Response Object

    Note over HTTPS: Stateless HTTP requests<br/>Session management required
    Note over TSO: Command interpretation<br/>Text parsing overhead
    Note over ZOSMF,TSO: Multiple concurrent requests may<br/>spawn many TSO address spaces<br/>(server configuration dependent)
```

## Component Architecture

### ZNP

```mermaid
graph TB
    subgraph "Client Components"
        subgraph "Presentation/Frontend Layer"
            CLI_CMD["CLI Commands<br/>• zssh ds list<br/>• zssh jobs submit<br/>• zssh uss upload"]
            VSC_UI["VS Code UI<br/>• Tree Views<br/>• File Operations<br/>• Job Management"]
        end

        subgraph "SDK Layer"
            ZSSH_CLIENT["ZSshClient<br/>• Connection Management<br/>• Request/Response"]
            RPC_CLIENT["AbstractRpcClient<br/>• Command Abstraction<br/>• ds.*, jobs.*, uss.* groups"]
        end

        CLI_CMD --> ZSSH_CLIENT
        VSC_UI --> ZSSH_CLIENT
        ZSSH_CLIENT --> RPC_CLIENT
    end

    subgraph "Server Components"
        subgraph "I/O Server (C++)"
            DISPATCHER["Command Dispatcher<br/>• Route by command name<br/>• JSON-RPC handling"]
            HANDLERS["Command Handlers"]
            WORKER_POOL["Worker Pool<br/>• Concurrent Processing<br/>• Request Queue"]
        end

        subgraph "Backend (C++)"
            ZDS["Dataset Module<br/>• VSAM Access<br/>• QSAM Operations"]
            ZJB["Job Module<br/>• JES Interface<br/>• Spool Access"]
            ZUSF["USS Module<br/>• File Operations<br/>• Directory Listing"]
            ZUTIL["Utility Modules<br/>• Memory Management<br/>• Error Handling"]
        end

        DISPATCHER --> HANDLERS
        HANDLERS --> WORKER_POOL
        HANDLERS --> ZDS
        HANDLERS --> ZJB
        HANDLERS --> ZUSF
        HANDLERS --> ZUTIL
    end

    RPC_CLIENT <--> DISPATCHER

    style CLI_CMD fill:#e3f2fd
    style VSC_UI fill:#e3f2fd
    style ZSSH_CLIENT fill:#f3e5f5
    style DISPATCHER fill:#e8f5e8
    style ZDS fill:#fff3e0
    style ZJB fill:#fff3e0
    style ZUSF fill:#fff3e0
```

### z/OSMF REST API

```mermaid
graph TB
    subgraph "Client Components"
        CLI_CMD2["CLI Commands<br/>zowe ds list, jobs submit"]
        VSC_UI2["VS Code Extension<br/>Zowe Explorer"]
        REST_CLIENT["REST Client SDK<br/>HTTP + JSON"]

        CLI_CMD2 --> REST_CLIENT
        VSC_UI2 --> REST_CLIENT
    end

    subgraph "Server Components"
        ZOSMF["z/OSMF Server<br/>Liberty + REST APIs"]
        TSO_AS["TSO Address Space<br/>Command Execution"]
        REXX["REXX Procedures<br/>Text Processing"]
    end

    REST_CLIENT <--> ZOSMF
    ZOSMF --> TSO_AS
    TSO_AS --> REXX

    style CLI_CMD2 fill:#e3f2fd
    style VSC_UI2 fill:#e3f2fd
    style REST_CLIENT fill:#f3e5f5
    style ZOSMF fill:#e8f5e8
```

## Performance and Capability

### Connection model

```mermaid
graph TB
    subgraph "Zowe Remote SSH"
        SSH_CONN["Single SSH Connection<br/>• Persistent<br/>• Multiplexed<br/>• Keep-alive"]
        SSH_BENEFIT["✅ Lower latency<br/>✅ Persistent state<br/>✅ Concurrent requests"]
        SSH_CONN --> SSH_BENEFIT
    end

    subgraph "z/OSMF REST API"
        HTTP_CONN["HTTP Connections<br/>• Stateless<br/>• Per-request<br/>• Connection pooling"]
        HTTP_BENEFIT["✅ Standard protocol<br/>✅ Stateless design<br/>✅ Web-friendly"]
        HTTP_CONN --> HTTP_BENEFIT
    end

    style SSH_CONN fill:#e8f5e8
    style HTTP_CONN fill:#fff3e0
    style SSH_BENEFIT fill:#e8f5e8
    style HTTP_BENEFIT fill:#fff3e0
```

### System resource usage

```mermaid
graph TB
    subgraph "Zowe Remote SSH"
        ZNP_RESOURCE["Resource Usage<br/>• Single SSH daemon process<br/>• Embedded zo server<br/>• Efficient connection reuse"]
        ZNP_SCALE["Scaling Behavior<br/>• Multiplexed requests<br/>• Worker pool management<br/>• Predictable resource usage"]
        ZNP_RESOURCE --> ZNP_SCALE
    end

    subgraph "z/OSMF REST API"
        ZOSMF_RESOURCE["Resource Usage<br/>• z/OSMF Liberty server<br/>• TSO address spaces<br/>• Per-request overhead"]
        ZOSMF_SCALE["Scaling Behavior<br/>• May spawn multiple TSO sessions<br/>• Resource usage varies by config<br/>• Potential address space proliferation"]
        ZOSMF_RESOURCE --> ZOSMF_SCALE
    end

    style ZNP_RESOURCE fill:#e8f5e8
    style ZNP_SCALE fill:#e8f5e8
    style ZOSMF_RESOURCE fill:#fff3e0
    style ZOSMF_SCALE fill:#ffebee
```

### Data processing

```mermaid
flowchart TD
    subgraph "Zowe Remote SSH Path"
        ZNP_START["Client Request"] --> ZNP_SSH["SSH Transport"]
        ZNP_SSH --> ZNP_JSON["JSON-RPC Parsing"]
        ZNP_JSON --> ZNP_CPP["C++ Command Handler"]
        ZNP_CPP --> ZNP_SYSTEM["Direct System APIs"]
        ZNP_SYSTEM --> ZNP_RESPONSE["Binary Response"]
        ZNP_RESPONSE --> ZNP_ENCODE["Base64 Encoding"]
        ZNP_ENCODE --> ZNP_RETURN["JSON Response"]
    end

    subgraph "z/OSMF REST API Path"
        ZOSMF_START["Client Request"] --> ZOSMF_HTTP["HTTPS Transport"]
        ZOSMF_HTTP --> ZOSMF_AUTH["Authentication"]
        ZOSMF_AUTH --> ZOSMF_PARSE["REST API Parsing"]
        ZOSMF_PARSE --> ZOSMF_TSO["TSO Command"]
        ZOSMF_TSO --> ZOSMF_REXX["REXX Execution"]
        ZOSMF_REXX --> ZOSMF_TEXT["Text Output"]
        ZOSMF_TEXT --> ZOSMF_TRANSFORM["Text Parsing"]
        ZOSMF_TRANSFORM --> ZOSMF_JSON["JSON Transform"]
        ZOSMF_JSON --> ZOSMF_RETURN["HTTP Response"]
    end

    ZNP_START -.->|"Fewer hops<br/>Direct access"| ZOSMF_START

    style ZNP_CPP fill:#e8f5e8
    style ZNP_SYSTEM fill:#e8f5e8
    style ZOSMF_TSO fill:#fff3e0
    style ZOSMF_REXX fill:#ffebee
```

## Security and Access Models

### ZNP

```mermaid
graph TB
    subgraph "SSH Security Model"
        SSH_AUTH["SSH Authentication<br/>• Private Key<br/>• Password<br/>• Multi-factor"]
        SSH_ENCRYPT["SSH Encryption<br/>• AES-256<br/>• RSA/ECDSA<br/>• Forward Secrecy"]
        SSH_TUNNEL["Secure Channel<br/>• Port forwarding<br/>• Compression<br/>• Integrity checks"]

        SSH_AUTH --> SSH_ENCRYPT
        SSH_ENCRYPT --> SSH_TUNNEL
    end

    subgraph "z/OS Authorization"
        ZOS_USER["z/OS User Context<br/>• RACF/ACF2/TSS<br/>• Dataset permissions<br/>• Resource access"]
        NATIVE_AUTH["Native Authorization<br/>• Direct SAF calls<br/>• APF authorization<br/>• System privileges"]

        SSH_TUNNEL --> ZOS_USER
        ZOS_USER --> NATIVE_AUTH
    end

    style SSH_AUTH fill:#e8f5e8
    style NATIVE_AUTH fill:#e8f5e8
```

### z/OSMF REST API

```mermaid
graph TB
    subgraph "HTTP Security Model"
        HTTPS_AUTH["HTTPS Authentication<br/>• Basic Auth<br/>• JWT Tokens<br/>• Certificates"]
        TLS_ENCRYPT["TLS Encryption<br/>• TLS 1.2/1.3<br/>• Certificate validation<br/>• Cipher suites"]
        HTTP_SESSION["Session Management<br/>• Token refresh<br/>• Timeout handling<br/>• CSRF protection"]

        HTTPS_AUTH --> TLS_ENCRYPT
        TLS_ENCRYPT --> HTTP_SESSION
    end

    subgraph "z/OSMF Authorization"
        ZOSMF_AUTH["z/OSMF Security<br/>• SAF integration<br/>• Role-based access<br/>• Resource profiles"]
        TSO_AUTH["TSO Authorization<br/>• Address space<br/>• Command authority<br/>• Resource access"]

        HTTP_SESSION --> ZOSMF_AUTH
        ZOSMF_AUTH --> TSO_AUTH
    end

    style HTTPS_AUTH fill:#fff3e0
    style TSO_AUTH fill:#fff3e0
```

## Use Cases

### High-frequency operations

```mermaid
sequenceDiagram
    participant Dev as Developer
    participant ZNP as Zowe Remote SSH
    participant ZOSMF as z/OSMF REST API

    Note over Dev: Scenario: Rapid file editing workflow

    Dev->>ZNP: Connect (SSH handshake)
    ZNP-->>Dev: Connected (persistent)

    Dev->>ZOSMF: Request 1 (HTTP + auth)
    ZOSMF-->>Dev: Response 1

    loop Multiple file operations
        Dev->>ZNP: Edit file (reuse connection)
        ZNP-->>Dev: Fast response

        Dev->>ZOSMF: Edit file (new HTTP request)
        ZOSMF-->>Dev: Response (auth overhead)
    end

    Note over ZNP: Lower latency for repeated operations
    Note over ZOSMF: Higher latency due to stateless nature
```

### Large data transfer

```mermaid
graph LR
    subgraph "Zowe Remote SSH"
        ZNP_STREAM["Streaming Support<br/>• Base64 chunks<br/>• Named pipes<br/>• Progress tracking"]
        ZNP_BINARY["Binary Handling<br/>• Direct encoding<br/>• Efficient transfer<br/>• Large file support"]
    end

    subgraph "z/OSMF REST API"
        ZOSMF_STREAM["Streaming Support<br/>• HTTP chunked transfer<br/>• Large file handling<br/>• Progress tracking"]
        ZOSMF_ISSUES["Known Issues<br/>• e-tag generation problems<br/>• Authentication overhead"]
    end

    ZNP_STREAM --> ZNP_BINARY
    ZOSMF_STREAM --> ZOSMF_ISSUES

    ZNP_BINARY -.->|"Different approaches<br/>for large files"| ZOSMF_STREAM

    style ZNP_STREAM fill:#e8f5e8
    style ZOSMF_STREAM fill:#fff3e0
    style ZOSMF_ISSUES fill:#ffebee
```
