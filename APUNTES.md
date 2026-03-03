# Apuntes: ft_irc - Servidor IRC en C++98

## Indice

1. [Que es IRC y que hace este proyecto](#1-que-es-irc-y-que-hace-este-proyecto)
2. [Estructura del proyecto](#2-estructura-del-proyecto)
3. [Teoria de redes: Sockets TCP](#3-teoria-de-redes-sockets-tcp)
4. [Teoria de redes: I/O no bloqueante y poll()](#4-teoria-de-redes-io-no-bloqueante-y-poll)
5. [Compilacion y ejecucion (Makefile)](#5-compilacion-y-ejecucion-makefile)
6. [main.cpp - Punto de entrada](#6-maincpp---punto-de-entrada)
7. [Clase Server - El cerebro del servidor](#7-clase-server---el-cerebro-del-servidor)
8. [Clase Client - Representacion de cada usuario](#8-clase-client---representacion-de-cada-usuario)
9. [Clase Channel - Las salas de chat](#9-clase-channel---las-salas-de-chat)
10. [El bucle principal: como funciona run()](#10-el-bucle-principal-como-funciona-run)
11. [Manejo de buffers y fragmentacion TCP](#11-manejo-de-buffers-y-fragmentacion-tcp)
12. [Registro de un cliente (PASS, NICK, USER)](#12-registro-de-un-cliente-pass-nick-user)
13. [Los comandos IRC implementados](#13-los-comandos-irc-implementados)
14. [Modos de canal](#14-modos-de-canal)
15. [Codigos numericos IRC](#15-codigos-numericos-irc)
16. [Conceptos clave de C++98 usados](#16-conceptos-clave-de-c98-usados)
17. [Patrones de diseno utilizados](#17-patrones-de-diseno-utilizados)
18. [Flujo completo de una conexion (ejemplo)](#18-flujo-completo-de-una-conexion-ejemplo)
19. [Resumen visual de la arquitectura](#19-resumen-visual-de-la-arquitectura)

---

## 1. Que es IRC y que hace este proyecto

### Que es IRC

**IRC (Internet Relay Chat)** es un protocolo de comunicacion en tiempo real basado en texto, creado en 1988. Funciona con un modelo **cliente-servidor**:

- Un **servidor** central gestiona las conexiones, los canales y los mensajes.
- Los **clientes** (programas como irssi, WeeChat, HexChat o incluso netcat) se conectan al servidor para chatear.

En IRC:
- Los usuarios se identifican con un **nickname** (apodo unico en todo el servidor).
- Los usuarios pueden unirse a **canales** (salas de chat grupales) cuyos nombres empiezan por `#` (por ejemplo `#general`).
- Los usuarios pueden enviar **mensajes privados** directamente a otros usuarios.
- Los canales tienen **operadores** con permisos especiales: expulsar usuarios, invitar, cambiar configuraciones, etc.

### Que hace este proyecto

Este programa es un **servidor IRC** escrito desde cero en C++98. No es un cliente (no tiene interfaz grafica ni de texto para chatear). Es el programa que:

1. Abre un puerto TCP y espera conexiones.
2. Recibe comandos de clientes IRC conectados.
3. Los procesa segun el protocolo IRC.
4. Reenvia mensajes entre usuarios y canales.
5. Gestiona permisos, modos, registros, etc.

**Uso:** `./ircserv <puerto> <contrasena>`

El servidor escucha en el puerto indicado y obliga a todos los clientes a autenticarse con la contrasena antes de poder usar cualquier comando.

### Protocolo basado en texto

El protocolo IRC es de texto plano. Cada comando es una linea terminada en `\r\n`. Ejemplos de lo que un cliente envia:

```
PASS secreto123\r\n
NICK alice\r\n
USER alice 0 * :Alice Smith\r\n
JOIN #general\r\n
PRIVMSG #general :Hola a todos!\r\n
```

Y el servidor responde con lineas tambien de texto:
```
:server 001 alice :Welcome to the ft_irc network\r\n
:alice JOIN :#general\r\n
:bob PRIVMSG #general :Bienvenida alice!\r\n
```

---

## 2. Estructura del proyecto

El proyecto tiene una estructura plana (sin subcarpetas):

```
proj-irc/
  |-- Makefile          -> Sistema de compilacion
  |-- README.md         -> Documentacion del proyecto
  |-- main.cpp          -> Punto de entrada del programa (32 lineas)
  |-- Server.hpp        -> Declaracion de la clase Server (65 lineas)
  |-- Server.cpp        -> Implementacion de la clase Server (584 lineas - el mas grande)
  |-- Client.hpp        -> Declaracion de la clase Client (43 lineas)
  |-- Client.cpp        -> Implementacion de la clase Client (70 lineas)
  |-- Channel.hpp       -> Declaracion de la clase Channel (58 lineas)
  |-- Channel.cpp       -> Implementacion de la clase Channel (71 lineas)
```

**Total:** ~920 lineas de codigo en 3 clases.

### Relacion entre las clases

```
          Server (gestiona TODO: red, comandos, estado)
          /                \
         /                  \
      Client              Channel
  (un usuario            (una sala de chat
   conectado)             con sus miembros)
```

- **Server** es la clase principal y mas compleja. Contiene un mapa de clientes y un mapa de canales. Toda la logica de red y procesamiento de comandos esta aqui.
- **Client** representa a un usuario conectado. Almacena su nickname, username, estado de registro y buffers de datos. No tiene logica de red.
- **Channel** representa una sala de chat. Almacena la lista de miembros, operadores, modos y configuracion. No tiene logica de red.

**Punto clave:** `Client` y `Channel` **no se conocen entre si**. Estan vinculados a traves de **file descriptors** (numeros enteros que identifican cada conexion). El `Server` actua como intermediario para toda comunicacion.

---

## 3. Teoria de redes: Sockets TCP

### Que es un socket

Un **socket** es un punto final de comunicacion de red. Es como un "enchufe" virtual: tu programa crea un socket, lo configura con una direccion IP y un puerto, y a traves de el envia y recibe datos.

En C/C++, un socket se representa como un **file descriptor (fd)**: un simple numero entero que el sistema operativo asigna para identificar recursos abiertos (archivos, conexiones de red, pipes, etc.).

```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);
//         ^familia     ^tipo         ^protocolo
```

- `AF_INET`: familia de direcciones IPv4 (Internet Protocol version 4: direcciones como 192.168.1.1).
- `SOCK_STREAM`: tipo TCP. TCP es un protocolo **orientado a conexion**: establece un canal fiable y ordenado entre dos extremos. Los datos llegan en orden y sin perdida. La alternativa seria `SOCK_DGRAM` para UDP (sin garantias de entrega ni orden).
- `0`: deja que el sistema operativo elija el protocolo adecuado (TCP para SOCK_STREAM).

### Los 5 pasos de un servidor TCP

Todo servidor TCP sigue estos pasos:

```
1. socket()   -> Crea el socket del servidor (obtiene un fd)
2. bind()     -> Asocia el socket a una direccion IP y un puerto
3. listen()   -> Pone el socket en modo "escuchando conexiones"
4. accept()   -> Acepta una conexion entrante (devuelve un NUEVO fd)
5. recv/send  -> Lee/escribe datos a traves del socket del cliente
   close()    -> Cierra la conexion cuando termina
```

Veamos cada uno en detalle:

### socket() - Crear el socket

```cpp
_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
if (_serverSocket < 0) throw SocketException();
```

Crea un nuevo socket y devuelve su file descriptor. Si devuelve un numero negativo, algo fallo (por ejemplo, el sistema se quedo sin recursos).

### setsockopt() - Opciones del socket

```cpp
int opt = 1;
setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

`SO_REUSEADDR` es una opcion que permite reutilizar la direccion/puerto inmediatamente despues de cerrar el servidor. Sin esto, si cierras el servidor y lo reabres rapido, el sistema operativo te dice "Address already in use" porque el puerto sigue reservado durante unos segundos (esto se llama **estado TIME_WAIT** de TCP, un mecanismo para asegurar que todos los paquetes pendientes se han entregado).

### bind() - Asignar direccion al socket

```cpp
struct sockaddr_in address;
std::memset(&address, 0, sizeof(address)); // Limpiar la estructura a ceros
address.sin_family = AF_INET;              // IPv4
address.sin_addr.s_addr = INADDR_ANY;      // Escuchar en TODAS las interfaces de red
address.sin_port = htons(_port);           // Puerto en formato de red

bind(_serverSocket, (struct sockaddr *)&address, sizeof(address));
```

**`sockaddr_in`** es la estructura que describe una direccion de red IPv4. Contiene:
- `sin_family`: la familia de direcciones (siempre `AF_INET` para IPv4).
- `sin_addr.s_addr`: la direccion IP. `INADDR_ANY` (0.0.0.0) significa "acepta conexiones desde cualquier interfaz de red" (WiFi, Ethernet, localhost...).
- `sin_port`: el numero de puerto.

**`htons()`** (Host TO Network Short): los procesadores pueden almacenar numeros de varias formas (little-endian o big-endian). El protocolo de red siempre usa big-endian. `htons()` convierte un numero de 2 bytes del formato de tu CPU al formato de la red. Sin esta conversion, un puerto como 6667 podria interpretarse como un numero completamente diferente.

**`bind()`** asocia el socket a esa direccion y puerto. Despues de bind, el socket "sabe" en que puerto debe escuchar.

El cast `(struct sockaddr *)` es necesario porque `bind()` acepta la interfaz generica `sockaddr`, pero nosotros usamos la version especifica para IPv4 (`sockaddr_in`).

### listen() - Empezar a aceptar conexiones

```cpp
listen(_serverSocket, SOMAXCONN);
```

Marca el socket como **pasivo**: ya no se usa para enviar/recibir datos, sino para escuchar intentos de conexion entrantes. `SOMAXCONN` es una constante del sistema que indica el tamano maximo de la cola de conexiones pendientes (conexiones que han llegado pero aun no han sido aceptadas con `accept()`).

### accept() - Aceptar una conexion nueva

```cpp
struct sockaddr_in clientAddr;
socklen_t clientLen = sizeof(clientAddr);
int clientFd = accept(_serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
```

Cuando un cliente se conecta (completa el TCP handshake), `accept()` crea un **nuevo socket** exclusivo para comunicarse con ese cliente especifico. El socket original (`_serverSocket`) sigue escuchando nuevas conexiones.

Piensa en ello como una recepcion de hotel: el recepcionista (`_serverSocket`) atiende a los nuevos huespedes y les asigna una habitacion (el `clientFd`). El recepcionista sigue disponible para el siguiente huesped.

### recv() y send() - Leer y escribir datos

```cpp
// Leer datos que envio el cliente
char buffer[1024];
int bytesRead = recv(fd, buffer, sizeof(buffer) - 1, 0);

// Enviar datos al cliente
int bytesSent = send(fd, data.c_str(), data.length(), 0);
```

- **`recv()`** lee datos del socket al buffer. Devuelve:
  - Numero positivo: cuantos bytes leyo.
  - 0: el cliente cerro la conexion limpiamente.
  - -1: error (si es no bloqueante y no hay datos, `errno = EAGAIN`, que no es un error real).

- **`send()`** envia datos por el socket. Devuelve cuantos bytes logro enviar. **Puede no enviar todo de una vez** si el buffer del sistema operativo esta lleno. Por eso se gestionan buffers de salida.

### close() - Cerrar el socket

```cpp
close(fd);
```

Libera el file descriptor y cierra la conexion TCP. Si no cierras los sockets, el sistema operativo se queda sin file descriptors disponibles (hay un limite).

---

## 4. Teoria de redes: I/O no bloqueante y poll()

### El problema del I/O bloqueante

Imagina un servidor con 100 clientes. Si usas `recv()` normal (bloqueante) con el cliente 1, tu programa se **queda parado** esperando hasta que ese cliente envie datos. Mientras tanto, los otros 99 clientes no pueden ser atendidos. Si el cliente 1 tarda 10 minutos, el servidor esta 10 minutos muerto.

Hay varias soluciones:
- **Un hilo por cliente**: funciona, pero crear hilos consume muchos recursos y es complicado de sincronizar. Este proyecto NO usa hilos.
- **I/O no bloqueante + multiplexacion**: la solucion elegida. Un solo hilo atiende a todos los clientes sin quedarse parado.

### fcntl() y el modo no bloqueante

```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
```

`fcntl()` (File CoNTroL) modifica las propiedades de un file descriptor. Con `O_NONBLOCK`, las funciones como `recv()`, `send()` y `accept()` **nunca se quedan esperando**. Si no hay datos disponibles, devuelven `-1` con `errno = EAGAIN` inmediatamente. Asi el programa puede seguir atendiendo a otros clientes.

### poll() - Vigilar multiples sockets a la vez

`poll()` es la pieza clave del servidor. Le dices "vigila estos 100 sockets y avisame cuando alguno tenga actividad":

```cpp
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
```

- `fds`: array de estructuras `pollfd`, una por cada socket a vigilar.
- `nfds`: cuantos sockets hay en el array.
- `timeout`: milisegundos de espera. `-1` = espera infinita hasta que pase algo.
- **Devuelve:** cuantos sockets tienen actividad (o -1 si hubo error).

### La estructura pollfd

```cpp
struct pollfd {
    int   fd;       // El file descriptor a vigilar
    short events;   // Que eventos nos interesan (LO RELLENAMOS NOSOTROS)
    short revents;  // Que eventos han ocurrido (LO RELLENA poll())
};
```

**`events`** es lo que le pedimos a poll() que vigile. **`revents`** es lo que poll() nos dice que paso realmente. Ambos usan **bitmasks** (combinacion de flags con bits):

| Evento     | Valor    | Significado |
|------------|----------|-------------|
| `POLLIN`   | En `events`/`revents` | Hay datos listos para leer, o hay una nueva conexion (en el socket servidor) |
| `POLLOUT`  | En `events`/`revents` | El socket esta listo para escribir sin bloquear |
| `POLLERR`  | Solo en `revents` | Error en el socket |
| `POLLHUP`  | Solo en `revents` | El otro extremo cerro la conexion (hang up) |
| `POLLNVAL` | Solo en `revents` | El file descriptor no es valido |

### Como se usan las bitmasks

Los eventos se combinan y comprueban con operaciones de bits:

```cpp
// Combinar flags: quiero vigilar POLLIN y POLLOUT
pfd.events = POLLIN | POLLOUT;

// Comprobar si un flag esta activo:
if (pfd.revents & POLLIN) { /* hay datos para leer */ }
if (pfd.revents & POLLOUT) { /* puedo escribir */ }
```

**Ejemplo visual:**
```
POLLIN  = 0000 0001  (bit 0)
POLLOUT = 0000 0100  (bit 2)

POLLIN | POLLOUT = 0000 0101  (bits 0 y 2 activos)

Si revents = 0000 0001:
  revents & POLLIN  = 0000 0001 -> TRUE (hay POLLIN)
  revents & POLLOUT = 0000 0000 -> FALSE (no hay POLLOUT)
```

### Activar y desactivar POLLOUT

El servidor necesita activar `POLLOUT` cuando tiene datos pendientes de enviar, y desactivarlo cuando ya no tiene nada (para no gastar CPU comprobando innecesariamente):

```cpp
void Server::setPollOut(int fd, bool enable) {
    for (size_t i = 0; i < _pollFds.size(); ++i) {
        if (_pollFds[i].fd == fd) {
            if (enable)
                _pollFds[i].events |= POLLOUT;   // Activar el bit de POLLOUT
            else
                _pollFds[i].events &= ~POLLOUT;  // Desactivar el bit de POLLOUT
            break;
        }
    }
}
```

- `|= POLLOUT`: OR -> activa el bit de POLLOUT sin tocar los demas bits.
- `&= ~POLLOUT`: AND NOT -> desactiva el bit de POLLOUT sin tocar los demas.

```
Ejemplo:
events  = 0000 0001  (solo POLLIN activo)
POLLOUT = 0000 0100

events |= POLLOUT  ->  0000 0101  (POLLIN + POLLOUT)

~POLLOUT = 1111 1011
events &= ~POLLOUT ->  0000 0001  (solo POLLIN)
```

---

## 5. Compilacion y ejecucion (Makefile)

### Makefile

```makefile
NAME     = ircserv
CXX      = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
SRCS     = main.cpp Server.cpp Client.cpp Channel.cpp
OBJS     = $(SRCS:.cpp=.o)
```

**Flags de compilacion:**
- `-Wall -Wextra`: activan la mayoria de warnings del compilador. Te avisan de cosas sospechosas en el codigo.
- `-Werror`: convierte TODOS los warnings en errores. El codigo no compila si hay un solo warning.
- `-std=c++98`: fuerza el estandar C++98. No se puede usar nada de C++11 ni posterior (nada de `auto`, `nullptr`, range-for loops, lambdas, etc.).

**Targets:**

| Comando | Que hace |
|---------|----------|
| `make` o `make all` | Compila todo y genera `ircserv` |
| `make clean` | Borra archivos objeto (.o) |
| `make fclean` | Borra .o Y el ejecutable |
| `make re` | Recompila desde cero (fclean + all) |

### Ejecutar el servidor

```bash
./ircserv 6667 micontrasena
# Puerto 6667 es el puerto estandar de IRC
```

### Conectar con un cliente IRC

Con irssi:
```
/connect localhost 6667 micontrasena
```

Con netcat (para pruebas manuales, muy util para depurar):
```bash
nc localhost 6667
PASS micontrasena
NICK alice
USER alice 0 * :Alice
JOIN #test
PRIVMSG #test :Hola mundo!
```

---

## 6. main.cpp - Punto de entrada

Archivo: `main.cpp` (32 lineas)

```cpp
int main(int argc, char **argv) {
    // 1. Validar que hay exactamente 2 argumentos (puerto y contrasena)
    if (argc != 3) {
        std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }

    // 2. Validar el puerto
    int port = std::atoi(argv[1]);   // Convierte string a int
    if (port <= 0 || port > 65535) { // Rango valido de puertos TCP
        std::cerr << "Error: Invalid port number." << std::endl;
        return 1;
    }

    // 3. Validar la contrasena
    std::string password = argv[2];
    if (password.empty()) { ... }

    // 4. Crear y ejecutar el servidor
    try {
        Server server(port, password); // Constructor: configura el socket
        server.run();                  // Bucle infinito de eventos
    } catch (std::exception &e) {
        // Si falla socket/bind/listen, se captura aqui
        std::cerr << "Server Initialization Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

**Lo que hace:**
1. Valida la entrada del usuario: necesita exactamente un puerto y una contrasena.
2. Comprueba que el puerto esta en rango valido (1-65535, que es el rango de puertos TCP).
3. Crea un objeto `Server` (el constructor configura toda la parte de red).
4. Llama a `server.run()` que es el **bucle infinito** -- aqui se queda el programa hasta que lo matas.
5. El `try-catch` captura las excepciones que el constructor puede lanzar si falla la creacion del socket, el bind o el listen.

---

## 7. Clase Server - El cerebro del servidor

Archivos: `Server.hpp` (65 lineas) / `Server.cpp` (584 lineas)

Esta es la clase mas grande e importante. Contiene TODA la logica: red, procesamiento de comandos, gestion de clientes y canales.

### 7.1 Atributos privados

```cpp
class Server {
private:
    int _port;                                  // Puerto TCP en el que escucha
    std::string _password;                      // Contrasena del servidor
    int _serverSocket;                          // FD del socket de escucha
    std::vector<struct pollfd> _pollFds;         // Todos los FDs vigilados por poll()
    std::map<int, Client> _clients;             // FD -> objeto Client
    std::map<std::string, Channel> _channels;   // nombre -> objeto Channel
};
```

**`_pollFds`**: un vector que contiene todos los file descriptors que `poll()` debe vigilar. El primer elemento es SIEMPRE el socket del servidor (para detectar nuevas conexiones). El resto son los sockets de los clientes conectados. Cuando un cliente se conecta, se anade un `pollfd` al vector; cuando se desconecta, se elimina.

**`_clients`**: un `std::map` que asocia cada file descriptor (int) con su objeto `Client`. Permite buscar rapidamente la informacion de un cliente dado su FD: `_clients[fd]`.

**`_channels`**: un `std::map` que asocia cada nombre de canal (string, ej: "#general") con su objeto `Channel`. Permite acceder rapidamente a un canal por su nombre.

### 7.2 Excepciones personalizadas

```cpp
class SocketException : public std::exception {
    public: const char* what() const throw() { return "Socket creation failed"; }
};
class BindException : public std::exception {
    public: const char* what() const throw() { return "Bind failed"; }
};
class ListenException : public std::exception {
    public: const char* what() const throw() { return "Listen failed"; }
};
```

Tres clases de excepcion que heredan de `std::exception`. Cada una sobreescribe `what()` para devolver un mensaje descriptivo.

- `throw()` en la firma es la forma de C++98 de decir "esta funcion no lanza excepciones" (en C++11 usarias `noexcept`).
- Se usan con `throw SocketException()` y se capturan con `catch(std::exception &e)` en `main()`.

### 7.3 Constructor y destructor

```cpp
Server::Server(int port, const std::string &password) : _port(port), _password(password) {
    setupServerSocket();  // Toda la inicializacion de red
}

Server::~Server() {
    for (size_t i = 0; i < _pollFds.size(); ++i) {
        close(_pollFds[i].fd);  // Cerrar TODOS los sockets abiertos
    }
}
```

El constructor usa **lista de inicializacion** (`: _port(port), _password(password)`) para inicializar los atributos antes de entrar al cuerpo del constructor. Luego llama a `setupServerSocket()`.

El destructor cierra TODOS los file descriptors abiertos (el del servidor y los de todos los clientes). Esto es importante para no dejar recursos del sistema operativo colgados ("file descriptor leak").

### 7.4 setupServerSocket() -- Inicializacion de red

`Server.cpp:22-55` - Realiza los 5 pasos clasicos de un servidor TCP:

```cpp
void Server::setupServerSocket() {
    // PASO 1: Crear el socket
    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket < 0) throw SocketException();

    // PASO 2: Configurar opciones (reutilizar puerto)
    int opt = 1;
    setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // PASO 3: Modo no bloqueante
    if (fcntl(_serverSocket, F_SETFL, O_NONBLOCK) < 0) {
        close(_serverSocket);
        throw SocketException();
    }

    // PASO 4: Bind - asociar a direccion y puerto
    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(_port);
    if (bind(_serverSocket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(_serverSocket);
        throw BindException();
    }

    // PASO 5: Listen - empezar a aceptar conexiones
    if (listen(_serverSocket, SOMAXCONN) < 0) {
        close(_serverSocket);
        throw ListenException();
    }

    // PASO 6: Anadir a poll() para vigilar nuevas conexiones
    struct pollfd srvPollFd;
    srvPollFd.fd = _serverSocket;
    srvPollFd.events = POLLIN;     // Solo nos interesa POLLIN (nueva conexion)
    srvPollFd.revents = 0;
    _pollFds.push_back(srvPollFd);
}
```

Si cualquier paso falla, se cierra el socket (para no dejar recursos colgados) y se lanza una excepcion que sera capturada en `main()`.

### 7.5 handleNewConnection() -- Aceptar un cliente

`Server.cpp:86-106` - Se llama cuando `poll()` detecta `POLLIN` en el socket del servidor (= alguien quiere conectarse):

```cpp
void Server::handleNewConnection() {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);

    // accept() crea un NUEVO socket dedicado a este cliente
    int clientFd = accept(_serverSocket, (struct sockaddr *)&clientAddr, &clientLen);

    if (clientFd >= 0) {
        fcntl(clientFd, F_SETFL, O_NONBLOCK);  // Modo no bloqueante

        // Crear pollfd para vigilar este cliente
        struct pollfd pfd;
        pfd.fd = clientFd;
        pfd.events = POLLIN;    // De momento solo nos interesa leer
        pfd.revents = 0;
        _pollFds.push_back(pfd);

        // Crear objeto Client y guardarlo
        _clients.insert(std::make_pair(clientFd, Client(clientFd)));
    }
}
```

**Concepto clave:** `accept()` devuelve un **NUEVO** file descriptor para cada cliente. El socket del servidor (`_serverSocket`) sigue escuchando nuevas conexiones, y el nuevo FD se usa exclusivamente para comunicarse con ese cliente en particular.

`std::make_pair(clientFd, Client(clientFd))` crea un par clave-valor para insertarlo en el `std::map`. Es la forma de C++98 de insertar en un mapa (en C++11 podrias usar `emplace` o `{}`).

### 7.6 handleClientRead() -- Leer datos del cliente

`Server.cpp:108-125` - Se llama cuando `poll()` detecta `POLLIN` en un socket de cliente:

```cpp
void Server::handleClientRead(int fd) {
    char buffer[1024];
    int bytesRead = recv(fd, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead <= 0) {
        // 0 = el cliente cerro la conexion
        // <0 = error
        removeClient(fd);
    } else {
        buffer[bytesRead] = '\0';
        if (_clients.find(fd) != _clients.end()) {
            Client &client = _clients[fd];
            client.appendInBuffer(buffer);      // Acumular datos

            std::string line;
            while (client.extractLine(line)) {  // Extraer lineas completas
                processCommand(client, line);   // Procesar cada comando
            }
        }
    }
}
```

**Por que no se procesa directamente lo que devuelve recv()?**
Porque TCP es un protocolo de **flujo de bytes**, no de mensajes. Un `recv()` puede traer:
- Medio comando: `"NIC"` (le falta el resto)
- Un comando completo: `"NICK alice\r\n"`
- Varios comandos juntos: `"NICK alice\r\nJOIN #general\r\n"`
- Cualquier combinacion

Por eso se acumulan los datos en un buffer y se extraen lineas completas (terminadas en `\r\n` o `\n`). Esto se explica en detalle en la seccion 11.

### 7.7 handleClientWrite() -- Enviar datos al cliente

`Server.cpp:127-144` - Se llama cuando `poll()` indica que el socket esta listo para escribir Y hay datos pendientes:

```cpp
void Server::handleClientWrite(int fd) {
    Client &client = _clients[fd];
    const std::string &out = client.getOutBuffer();

    if (!out.empty()) {
        int bytesSent = send(fd, out.c_str(), out.length(), 0);
        if (bytesSent > 0) {
            client.eraseOutBuffer(bytesSent); // Solo borrar lo que se envio
        }
    }

    if (client.getOutBuffer().empty()) {
        setPollOut(fd, false);  // Buffer vacio -> ya no necesitamos POLLOUT
    }
}
```

**Por que no enviar directamente con send()?**
Porque `send()` puede no enviar todos los bytes de una vez (si el buffer del SO esta lleno). El patron correcto es:

1. Encolar los datos en el buffer de salida del cliente.
2. Activar `POLLOUT` para que `poll()` avise cuando se pueda escribir.
3. Cuando poll() lo indique, enviar lo que se pueda con `send()`.
4. Borrar del buffer solo lo que se consiguio enviar.
5. Si queda algo, se reintenta en la proxima iteracion.
6. Cuando el buffer queda vacio, desactivar `POLLOUT`.

### 7.8 sendToClient() y broadcastToChannel() -- Funciones de envio

`Server.cpp:156-173`

```cpp
void Server::sendToClient(int fd, const std::string &msg) {
    _clients[fd].appendOutBuffer(msg + "\r\n");  // Encolar con terminador IRC
    setPollOut(fd, true);                         // Activar POLLOUT
}

void Server::broadcastToChannel(const std::string &channelName, const std::string &msg, int senderFd) {
    const std::vector<int> &clients = _channels[channelName].getClients();
    for (size_t i = 0; i < clients.size(); ++i) {
        if (clients[i] != senderFd) {  // A todos MENOS al remitente
            sendToClient(clients[i], msg);
        }
    }
}
```

**`sendToClient`** NUNCA llama a `send()` directamente. Siempre encola el mensaje en el buffer y deja que `handleClientWrite` se encargue. Anade automaticamente `\r\n` al final (terminador de linea IRC).

**`broadcastToChannel`** envia un mensaje a todos los miembros de un canal excepto al que lo origino (`senderFd`). Si `senderFd` es `-1`, el mensaje llega a TODOS incluido el remitente (se usa por ejemplo en JOIN, donde todos deben ver que alguien entro).

### 7.9 getClientByNick() -- Buscar por nickname

`Server.cpp:175-182`

```cpp
Client* Server::getClientByNick(const std::string &nick) {
    for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (it->second.getNickname() == nick) {
            return &(it->second);
        }
    }
    return NULL;
}
```

Busqueda lineal por todo el mapa de clientes comparando nicknames. Devuelve un puntero al Client encontrado, o `NULL` si no existe. Se usa para comandos como PRIVMSG, KICK, INVITE, etc.

En C++98 no existe `nullptr`, se usa `NULL` (que internamente es 0).

### 7.10 removeClient() -- Desconectar y limpiar

`Server.cpp:184-207`

```cpp
void Server::removeClient(int fd) {
    // 1. Eliminar del vector de poll
    for (std::vector<struct pollfd>::iterator it = _pollFds.begin(); it != _pollFds.end(); ++it) {
        if (it->fd == fd) {
            _pollFds.erase(it);
            break;
        }
    }

    // 2. Eliminar de TODOS los canales
    for (std::map<std::string, Channel>::iterator it = _channels.begin();
         it != _channels.end(); /* incremento manual */) {
        it->second.removeClient(fd);
        if (it->second.isEmpty()) {
            // Canal vacio -> eliminarlo
            std::map<std::string, Channel>::iterator to_erase = it;
            ++it;                      // Avanzar ANTES de borrar
            _channels.erase(to_erase); // Ahora borrar
        } else {
            ++it;
        }
    }

    // 3. Eliminar del mapa de clientes
    _clients.erase(fd);

    // 4. Cerrar el socket
    close(fd);
}
```

**Nota importante sobre iteradores:** Cuando borras un elemento de un `std::map` mientras iteras sobre el, el iterador al elemento borrado queda **invalidado** (apunta a memoria liberada). Si haces `++it` despues de borrar, el programa crashea.

La solucion es guardar una copia del iterador (`to_erase`), avanzar el iterador original (`++it`), y LUEGO borrar usando la copia. Este es un patron clasico en C++98. En C++11, `erase()` devuelve el siguiente iterador valido, pero en C++98 no.

### 7.11 processCommand() -- El enrutador de comandos

`Server.cpp:209-250`

```cpp
void Server::processCommand(Client &client, const std::string &command) {
    if (command.empty()) return;

    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;  // Extrae la primera palabra (el nombre del comando)

    // Comandos de PRE-registro (siempre permitidos)
    if (cmd == "PASS")      { cmdPass(client, iss); }
    else if (cmd == "NICK") { cmdNick(client, iss); }
    else if (cmd == "USER") { cmdUser(client, iss); }
    else if (cmd == "QUIT") { removeClient(client.getFd()); }
    else {
        // El resto de comandos REQUIEREN estar registrado
        if (!client.isRegistered()) {
            sendToClient(client.getFd(), ":server 451 " + client.getNickname()
                         + " :You have not registered");
            return;
        }

        // Comandos de POST-registro
        if (cmd == "JOIN")        { cmdJoin(client, iss); }
        else if (cmd == "PRIVMSG"){ cmdPrivmsg(client, iss, false); }
        else if (cmd == "NOTICE") { cmdPrivmsg(client, iss, true); }
        else if (cmd == "KICK")   { cmdKick(client, iss); }
        else if (cmd == "INVITE") { cmdInvite(client, iss); }
        else if (cmd == "TOPIC")  { cmdTopic(client, iss); }
        else if (cmd == "MODE")   { cmdMode(client, iss); }
        else {
            sendToClient(client.getFd(), ":server 421 " + client.getNickname()
                         + " " + cmd + " :Unknown command");
        }
    }
}
```

Usa `std::istringstream` para parsear la linea. El `iss >> cmd` extrae la primera palabra y deja el resto del stream disponible para que cada funcion handler extraiga sus propios parametros. Cada comando IRC tiene su propia funcion (`cmdPass`, `cmdNick`, `cmdJoin`, etc.).

**Regla de registro:** PASS, NICK, USER y QUIT se permiten siempre (porque los necesitas para registrarte). Cualquier otro comando antes de registrarse devuelve error 451.

---

## 8. Clase Client - Representacion de cada usuario

Archivos: `Client.hpp` (43 lineas) / `Client.cpp` (70 lineas)

Clase de datos pura. No tiene logica de red ni conoce al servidor.

### 8.1 Atributos

```cpp
class Client {
private:
    int         _fd;           // File descriptor del socket de este cliente
    std::string _nickname;     // Apodo IRC (ej: "alice")
    std::string _username;     // Nombre de usuario IRC (ej: "alice")
    bool        _hasPassed;    // true si envio PASS con la contrasena correcta
    bool        _isRegistered; // true si completo PASS + NICK + USER
    std::string _inBuffer;     // Buffer de datos entrantes (acumula bytes de recv)
    std::string _outBuffer;    // Buffer de datos salientes (cola de envio)
};
```

### 8.2 Constructores

```cpp
Client::Client() : _fd(-1), _hasPassed(false), _isRegistered(false) {}
Client::Client(int fd) : _fd(fd), _hasPassed(false), _isRegistered(false) {}
```

El constructor por defecto (sin parametros) existe porque `std::map` lo requiere internamente. Pone `_fd = -1` como valor invalido.

El constructor con parametro recibe el file descriptor asignado al cliente.

Ambos empiezan con `_hasPassed = false` y `_isRegistered = false`: el cliente no esta registrado hasta que complete los tres pasos.

### 8.3 Mecanismo de registro

```cpp
void Client::setNickname(const std::string &nick) {
    _nickname = nick;
    checkRegistration();  // Comprobar si ya se cumplen las 3 condiciones
}

void Client::setUsername(const std::string &user) {
    _username = user;
    checkRegistration();  // Comprobar si ya se cumplen las 3 condiciones
}

void Client::checkRegistration() {
    if (_hasPassed && !_nickname.empty() && !_username.empty()) {
        _isRegistered = true;
    }
}
```

`checkRegistration()` se llama automaticamente cada vez que se cambia el nickname o el username. Cuando las tres condiciones se cumplen (password correcta + nickname no vacio + username no vacio), el cliente pasa a estar registrado.

### 8.4 Operaciones de buffer

```cpp
// --- BUFFER DE ENTRADA ---
void Client::appendInBuffer(const std::string &data) {
    _inBuffer += data;
}

bool Client::extractLine(std::string &line) {
    size_t pos = _inBuffer.find("\r\n");        // Buscar \r\n primero
    if (pos == std::string::npos)
        pos = _inBuffer.find('\n');              // Si no, buscar \n solo

    if (pos != std::string::npos) {
        line = _inBuffer.substr(0, pos);         // Extraer linea SIN terminador
        size_t skip = (_inBuffer[pos] == '\r') ? 2 : 1;  // \r\n=2, \n=1
        _inBuffer.erase(0, pos + skip);          // Borrar linea del buffer
        return true;                              // Hay linea completa
    }
    return false;                                 // No hay linea completa aun
}

// --- BUFFER DE SALIDA ---
void Client::appendOutBuffer(const std::string &data) {
    _outBuffer += data;
}

const std::string& Client::getOutBuffer() const {
    return _outBuffer;
}

void Client::eraseOutBuffer(size_t n) {
    if (n <= _outBuffer.length()) {
        _outBuffer.erase(0, n);
    }
}
```

Estos buffers son el mecanismo que resuelve el problema de la fragmentacion TCP (ver seccion 11 para una explicacion detallada).

---

## 9. Clase Channel - Las salas de chat

Archivos: `Channel.hpp` (58 lineas) / `Channel.cpp` (71 lineas)

Clase de datos pura. No tiene logica de red.

### 9.1 Atributos

```cpp
class Channel {
private:
    std::string _name;                // Nombre del canal (ej: "#general")
    std::string _topic;               // Tema/descripcion del canal
    std::vector<int> _clientsFds;     // FDs de TODOS los miembros
    std::vector<int> _operatorsFds;   // FDs de los operadores (subconjunto de miembros)
    bool _inviteOnly;                 // Modo +i: solo invitados pueden entrar
    bool _topicRestricted;            // Modo +t: solo operadores cambian el topic
    std::string _password;            // Modo +k: contrasena para entrar al canal
    size_t _userLimit;                // Modo +l: maximo de usuarios (0 = sin limite)
    std::vector<int> _invitedFds;     // Lista blanca de invitados (para modo +i)
};
```

**Nota importante:** Los canales NO guardan punteros ni referencias a objetos `Client`. Solo guardan sus file descriptors (numeros enteros). Cuando el servidor necesita hacer algo con un miembro del canal, busca el `Client` correspondiente en su mapa `_clients[fd]`. Este desacoplamiento hace que `Channel` sea completamente independiente de `Client`.

### 9.2 Valores por defecto al crear un canal

```cpp
Channel::Channel(const std::string &name)
    : _name(name), _inviteOnly(false), _topicRestricted(true),
      _password(""), _userLimit(0) {}
```

| Atributo | Valor inicial | Significado |
|----------|---------------|-------------|
| `_inviteOnly` | `false` | Cualquiera puede entrar |
| `_topicRestricted` | **`true`** | Solo operadores pueden cambiar el topic |
| `_password` | `""` (vacio) | Sin contrasena |
| `_userLimit` | `0` | Sin limite de usuarios |

### 9.3 Gestion de miembros

```cpp
void Channel::addClient(int fd) {
    if (!hasClient(fd))                 // Evitar duplicados
        _clientsFds.push_back(fd);
}

void Channel::removeClient(int fd) {
    // Patron erase-remove:
    _clientsFds.erase(
        std::remove(_clientsFds.begin(), _clientsFds.end(), fd),
        _clientsFds.end()
    );
    removeOperator(fd);  // Si era operador, tambien quitarlo
}

bool Channel::hasClient(int fd) const {
    return std::find(_clientsFds.begin(), _clientsFds.end(), fd) != _clientsFds.end();
}
```

**El patron erase-remove:**
`std::remove` NO borra elementos del vector. Lo que hace es mover todos los elementos que NO coinciden al principio del vector y devolver un iterador al "nuevo final logico". Los elementos despues de ese iterador son basura. Luego `erase()` borra desde ese punto hasta el final real.

```
Antes:     [3, 7, 5, 7, 9]    remove(7)
Despues:   [3, 5, 9, ?, ?]    ^ erase desde aqui
Resultado: [3, 5, 9]
```

### 9.4 Gestion de operadores e invitados

```cpp
void Channel::addOperator(int fd) {
    if (!isOperator(fd)) _operatorsFds.push_back(fd);
}

void Channel::removeOperator(int fd) {
    _operatorsFds.erase(std::remove(...), _operatorsFds.end());
}

void Channel::inviteClient(int fd) {
    if (!isInvited(fd)) _invitedFds.push_back(fd);
}
```

Misma logica que con los miembros normales. Los operadores son un subconjunto de los miembros. Los invitados son una lista blanca aparte que solo se consulta cuando el modo `+i` esta activo.

### 9.5 isEmpty()

```cpp
bool Channel::isEmpty() const {
    return _clientsFds.empty();
}
```

El servidor comprueba esto despues de eliminar un cliente de un canal. Si el canal queda vacio, se borra del mapa `_channels` para no acumular canales fantasma.

---

## 10. El bucle principal: como funciona run()

`Server.cpp:57-84`

```cpp
void Server::run() {
    std::cout << "IRC Server started on port " << _port << std::endl;

    while (true) {
        // 1. BLOQUEAR hasta que haya actividad en algun socket
        if (poll(&_pollFds[0], _pollFds.size(), -1) < 0) {
            std::cerr << "Poll error." << std::endl;
            break;
        }

        // 2. RECORRER todos los FDs buscando actividad
        for (size_t i = 0; i < _pollFds.size(); ++i) {
            // Hay datos para leer?
            if (_pollFds[i].revents & POLLIN) {
                if (_pollFds[i].fd == _serverSocket) {
                    handleNewConnection();   // Nueva conexion TCP
                } else {
                    handleClientRead(_pollFds[i].fd);  // Datos de un cliente
                }
            }
            // Se puede escribir?
            if (_pollFds[i].revents & POLLOUT) {
                handleClientWrite(_pollFds[i].fd);
            }
            // Error o desconexion?
            if (_pollFds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (_pollFds[i].fd != _serverSocket) {
                    removeClient(_pollFds[i].fd);
                }
            }
        }
    }
}
```

**Este es el corazon del servidor.** Todo el programa gira alrededor de este bucle infinito:

```
 +---> poll() se bloquea esperando actividad
 |         |
 |         v
 |     Recorrer todos los FDs:
 |         |
 |         |-- FD del servidor + POLLIN?   -> handleNewConnection()
 |         |-- FD de cliente + POLLIN?     -> handleClientRead(fd)
 |         |-- FD de cliente + POLLOUT?    -> handleClientWrite(fd)
 |         |-- FD de cliente + POLLERR?    -> removeClient(fd)
 |         |
 +-------- Volver arriba
```

**Diagrama temporal de ejemplo:**

```
Tiempo -->

poll() bloqueado ... (nada pasa, el servidor espera)
     |
     v
Cliente A envia "NICK alice\r\n"        (POLLIN en FD 5)
     |
     v
handleClientRead(5)
  -> recv() lee "NICK alice\r\n"
  -> appendInBuffer("NICK alice\r\n")
  -> extractLine() -> "NICK alice"
  -> processCommand() -> cmdNick()
  -> sendToClient(5, ":server 001 alice :Welcome...")
     -> appendOutBuffer(":server 001 alice :Welcome...\r\n")
     -> setPollOut(5, true)
     |
     v
poll() bloqueado ... (detecta POLLOUT en FD 5)
     |
     v
handleClientWrite(5)
  -> send() envia el mensaje de bienvenida
  -> eraseOutBuffer(bytes_enviados)
  -> Buffer vacio -> setPollOut(5, false)
     |
     v
poll() bloqueado ... (espera siguiente evento)
```

---

## 11. Manejo de buffers y fragmentacion TCP

### El problema fundamental

TCP es un protocolo de **flujo de bytes** (byte stream). No entiende de "mensajes" ni de "lineas". Cuando un cliente envia:

```
NICK alice\r\n
JOIN #general\r\n
```

El servidor puede recibirlo de CUALQUIERA de estas formas:

```
Caso 1 (todo junto):    "NICK alice\r\nJOIN #general\r\n"
Caso 2 (partido):       "NICK ali" + "ce\r\nJOIN #general\r\n"
Caso 3 (muy partido):   "NI" + "CK al" + "ice\r\nJO" + "IN #general\r\n"
Caso 4 (mezclado):      "NICK alice\r\nJOIN #gen" + "eral\r\n"
```

Cada llamada a `recv()` puede devolver cualquier cantidad de bytes. No hay garantia de que un `recv()` contenga exactamente un comando completo.

### La solucion: buffer de entrada + extractLine()

```
recv() --datos parciales--> appendInBuffer() --acumula--> _inBuffer
                                                              |
                                                       extractLine()
                                                              |
                                                   Busca \r\n o \n
                                                              |
                                                    +---SI----+---NO---+
                                                    |                  |
                                              Extrae linea     Devuelve false
                                              Borra del buffer  (espera mas datos)
                                              Devuelve true
                                                    |
                                              processCommand(linea)
```

**Ejemplo paso a paso:**

```
recv() #1: "NICK ali"
  _inBuffer = "NICK ali"
  extractLine() -> no encuentra \r\n -> false -> no se procesa nada

recv() #2: "ce\r\nJOIN #gen"
  _inBuffer = "NICK alice\r\nJOIN #gen"
  extractLine() -> encuentra \r\n en posicion 10
    line = "NICK alice"
    _inBuffer = "JOIN #gen"   (se borro la linea extraida)
    -> processCommand("NICK alice")
  extractLine() -> no encuentra \r\n -> false -> espera

recv() #3: "eral\r\n"
  _inBuffer = "JOIN #general\r\n"
  extractLine() -> encuentra \r\n en posicion 13
    line = "JOIN #general"
    _inBuffer = ""
    -> processCommand("JOIN #general")
  extractLine() -> buffer vacio -> false -> espera
```

### Buffer de salida

El buffer de salida (`_outBuffer`) resuelve un problema similar pero para el envio. `send()` puede no enviar todos los bytes de una vez si el buffer de red del sistema operativo esta lleno.

```
sendToClient("mensaje")
  |
  v
appendOutBuffer("mensaje\r\n") + setPollOut(fd, true)
  |
  v
poll() detecta POLLOUT (el socket puede escribir)
  |
  v
handleClientWrite()
  |
  v
send(fd, outBuffer, length)
  |
  v
bytesSent = lo que se logro enviar (puede ser MENOS que length)
  |
  v
eraseOutBuffer(bytesSent)    <- Borrar SOLO lo enviado
  |
  v
Si buffer vacio: setPollOut(fd, false)
Si queda algo: se reintenta en la proxima iteracion de poll()
```

---

## 12. Registro de un cliente (PASS, NICK, USER)

Antes de poder usar cualquier comando (excepto PASS, NICK, USER y QUIT), un cliente debe completar el proceso de registro en tres pasos:

```
Cliente conecta -> _hasPassed=false, _isRegistered=false
  |
  |-- PASS secreto123    -> Si coincide con la contrasena del servidor: _hasPassed = true
  |-- NICK alice         -> _nickname = "alice", checkRegistration() (falta username)
  |-- USER alice 0 * :x  -> _username = "alice", checkRegistration() -> TODO OK
  |                          _isRegistered = true
  |
  v
Servidor envia: ":server 001 alice :Welcome to the ft_irc network"
  |
  v
Ahora alice puede usar JOIN, PRIVMSG, KICK, MODE, etc.
```

### cmdPass() -- `Server.cpp:252-260`

```cpp
void Server::cmdPass(Client &client, std::istringstream &iss) {
    std::string pass;
    iss >> pass;
    if (pass == _password) {
        client.setPassed(true);
    } else {
        sendToClient(client.getFd(), ":server 464 :Password incorrect");
    }
}
```

Compara la contrasena enviada con `_password` del servidor. Si coincide, marca `_hasPassed = true`. Si no, error 464.

### cmdNick() -- `Server.cpp:262-283`

```cpp
void Server::cmdNick(Client &client, std::istringstream &iss) {
    std::string nick;
    iss >> nick;

    if (nick.empty()) {
        sendToClient(client.getFd(), ":server 431 :No nickname given");
        return;
    }
    if (getClientByNick(nick) != NULL) {
        sendToClient(client.getFd(), ":server 433 * " + nick + " :Nickname is already in use");
        return;
    }

    std::string oldNick = client.getNickname();
    client.setNickname(nick);  // Esto llama a checkRegistration() internamente

    if (client.isRegistered() && !oldNick.empty()) {
        // Cambio de nick estando ya registrado
        sendToClient(client.getFd(), ":" + oldNick + " NICK :" + nick);
    } else if (client.isRegistered() && oldNick.empty()) {
        // Acaba de completar el registro (primer nick)
        sendToClient(client.getFd(), ":server 001 " + nick + " :Welcome to the ft_irc network");
    }
}
```

Validaciones:
1. Que no este vacio (error 431).
2. Que no lo este usando otro cliente (error 433).

Dos casos despues de asignar el nick:
- Si ya estaba registrado y tenia nick (cambio de nick): notifica con `:oldnick NICK :newnick`.
- Si acaba de completar el registro: envia el mensaje de bienvenida (001).

### cmdUser() -- `Server.cpp:285-298`

```cpp
void Server::cmdUser(Client &client, std::istringstream &iss) {
    if (client.isRegistered()) {
        sendToClient(client.getFd(), ":server 462 :You may not reregister");
        return;
    }
    std::string user;
    iss >> user;
    if (!user.empty()) {
        client.setUsername(user);  // Esto llama a checkRegistration() internamente
        if (client.isRegistered()) {
            sendToClient(client.getFd(),
                ":server 001 " + client.getNickname() + " :Welcome to the ft_irc network");
        }
    }
}
```

Si ya esta registrado: error 462 (no puedes re-registrarte). Si no, guarda el username y comprueba si ahora se cumplen las tres condiciones. Si si, envia bienvenida.

---

## 13. Los comandos IRC implementados

### JOIN -- Unirse a un canal (`Server.cpp:300-335`)

```
Formato: JOIN <#canal> [clave]
```

Flujo detallado:

1. Validar que el nombre empiece por `#` (error 461 si no).
2. Si el canal **no existe**:
   - Se crea: `_channels[chanName] = Channel(chanName)`
   - El creador se convierte automaticamente en **operador**: `addOperator(client.getFd())`
3. Si el canal **ya existe**, comprobar restricciones:
   - Modo `+i` (invite only): el usuario debe estar en `_invitedFds` (error 473).
   - Modo `+k` (password): la clave debe coincidir (error 475).
   - Modo `+l` (limite): no debe superar el maximo de usuarios (error 471).
4. Anadir al usuario al canal: `addClient(client.getFd())`
5. Broadcast JOIN a todos los miembros (incluido el nuevo): `broadcastToChannel(chanName, msg, -1)`
   - Se pasa `-1` como senderFd para que le llegue a TODOS, incluido el que entra.
6. Si hay topic configurado, enviarlo al nuevo miembro (codigo 332).

### PRIVMSG y NOTICE -- Enviar mensajes (`Server.cpp:337-366`)

```
Formato: PRIVMSG <destino> :<texto>
         NOTICE <destino> :<texto>
```

- Si `destino` empieza por `#` -> **mensaje a canal**:
  - Verificar que el canal existe y que el emisor es miembro (error 404).
  - Broadcast a todos los miembros excepto al emisor.
- Si no -> **mensaje privado a usuario**:
  - Buscar al usuario por nickname (error 401 si no existe).
  - Enviar directamente al destinatario.

**Diferencia entre PRIVMSG y NOTICE:** Ambos son identicos en funcionamiento, pero NOTICE **nunca genera errores de vuelta**. Esto es parte del estandar IRC para evitar bucles infinitos (por ejemplo, dos bots que se mandan NOTICE y la respuesta de error genera otro NOTICE, que genera otro error...).

En el codigo, `cmdPrivmsg` recibe un parametro `bool isNotice`. Si es `true`, todas las lineas de `sendToClient` con errores se saltan.

### KICK -- Expulsar de un canal (`Server.cpp:368-405`)

```
Formato: KICK <#canal> <nickname> [razon]
```

Solo operadores pueden usarlo. Cadena de validaciones:
1. El canal existe (error 403).
2. El que ejecuta KICK esta en el canal (error 442).
3. El que ejecuta KICK es operador (error 482).
4. El objetivo existe y esta en el canal (error 441).

Si todo OK: broadcast KICK a todo el canal (para que todos vean la expulsion) y luego `removeClient(targetFd)` del canal.

### INVITE -- Invitar a un canal (`Server.cpp:407-446`)

```
Formato: INVITE <nickname> <#canal>
```

Validaciones:
1. El canal existe (error 403).
2. El que invita esta en el canal (error 442).
3. Si el canal es `+i` (invite only), el que invita debe ser operador (error 482).
4. El objetivo existe (error 401).
5. El objetivo no esta ya en el canal (error 443).

Si todo OK:
- Anade al objetivo a la whitelist: `ch.inviteClient(targetClient->getFd())`
- Confirma al invitador (codigo 341).
- Notifica al invitado.

### TOPIC -- Ver o cambiar el tema del canal (`Server.cpp:448-495`)

```
Formato: TOPIC <#canal>            -> consultar el topic
         TOPIC <#canal> :<texto>   -> cambiar el topic
```

- Sin argumento despues del canal: devuelve el topic actual (332) o "No topic is set" (331).
- Con argumento:
  - Si el modo `+t` esta activo (por defecto lo esta), solo operadores pueden cambiar el topic (error 482).
  - Si `+t` no esta activo, cualquier miembro puede cambiarlo.
  - Broadcast del cambio a todo el canal.

### MODE -- Cambiar modos del canal (`Server.cpp:497-584`)

```
Formato: MODE <#canal>                      -> consultar modos activos
         MODE <#canal> <+/-modos> [args]    -> cambiar modos
```

**Consultar modos** (sin argumentos despues del canal):
- Construye una cadena con los modos activos: `"+itk"`, `"+t"`, etc.
- Envia codigo 324 con los modos.

**Cambiar modos** (con argumentos):
- Solo operadores pueden cambiar modos (error 482).
- El parser recorre la cadena de modos caracter a caracter:
  - `+` -> los siguientes caracteres ACTIVAN modos.
  - `-` -> los siguientes caracteres DESACTIVAN modos.
  - `i` -> activa/desactiva invite-only.
  - `t` -> activa/desactiva restriccion de topic.
  - `k` -> activa/desactiva password (al activar, lee el siguiente parametro como clave).
  - `l` -> activa/desactiva limite de usuarios (al activar, lee el siguiente parametro como numero).
  - `o` -> da/quita operador (lee el siguiente parametro como nickname).

Ejemplo: `MODE #general +ik secreto`
1. `+` -> modo "activar".
2. `i` -> activa invite-only.
3. `k` -> activa password, lee "secreto" del stream.

Cada cambio se broadcast al canal.

---

## 14. Modos de canal

| Modo | Letra | Parametro | Efecto | Activo por defecto |
|------|-------|-----------|---------|--------------------|
| Invite Only | `i` | No | Solo usuarios invitados (con INVITE) pueden hacer JOIN | No |
| Topic Restricted | `t` | No | Solo operadores pueden cambiar el TOPIC | **Si** |
| Channel Key | `k` | Si (la clave al activar) | Se necesita clave para hacer JOIN | No |
| User Limit | `l` | Si (el numero al activar) | Limita el numero maximo de miembros | No |
| Operator | `o` | Si (el nickname) | Da o quita privilegios de operador a un usuario | N/A |

### Que puede hacer un operador

Un operador de canal (marcado con `+o`) tiene estos privilegios exclusivos:
- `KICK`: expulsar a otros usuarios del canal.
- `TOPIC`: cambiar el tema del canal cuando `+t` esta activo.
- `INVITE`: invitar usuarios cuando `+i` esta activo.
- `MODE`: cambiar cualquier modo del canal.
- `+o`/`-o`: dar o quitar operador a otros usuarios.

**El primer usuario que hace JOIN a un canal nuevo se convierte automaticamente en operador** (`Server.cpp:310`).

### Ejemplos de uso de MODE

```
MODE #general +i              -> Activar invite-only
MODE #general -i              -> Desactivar invite-only
MODE #general +k secreto     -> Poner contrasena "secreto"
MODE #general -k              -> Quitar contrasena
MODE #general +l 10           -> Maximo 10 usuarios
MODE #general -l              -> Sin limite
MODE #general +o pepito       -> Hacer operador a pepito
MODE #general -o pepito       -> Quitar operador a pepito
MODE #general +itk password   -> Activar invite-only, topic restricted y poner password
MODE #general -i+l 20         -> Desactivar invite-only y poner limite de 20
```

---

## 15. Codigos numericos IRC

El protocolo IRC define codigos numericos de 3 digitos para las respuestas del servidor (similar a HTTP: 200 OK, 404 Not Found). Los mensajes siguen este formato:

```
:server <codigo> <destinatario> [parametros] :<texto>
```

### Codigos de exito

| Codigo | Nombre | Cuando se usa |
|--------|--------|---------------|
| 001 | RPL_WELCOME | Registro completado con exito |
| 324 | RPL_CHANNELMODEIS | Respuesta a `MODE #canal` (sin argumentos): muestra modos activos |
| 331 | RPL_NOTOPIC | El canal no tiene topic configurado |
| 332 | RPL_TOPIC | Muestra el topic actual del canal |
| 341 | RPL_INVITING | Confirmacion de que INVITE se envio correctamente |

### Codigos de error

| Codigo | Nombre | Cuando se envia |
|--------|--------|-----------------|
| 401 | ERR_NOSUCHNICK | El nickname objetivo no existe (PRIVMSG, INVITE a nadie) |
| 403 | ERR_NOSUCHCHANNEL | El canal no existe (KICK, INVITE, TOPIC en canal inexistente) |
| 404 | ERR_CANNOTSENDTOCHAN | Intentas enviar PRIVMSG a un canal del que no eres miembro |
| 411 | ERR_NORECIPIENT | PRIVMSG/NOTICE sin destinatario o sin texto |
| 421 | ERR_UNKNOWNCOMMAND | Comando no reconocido por el servidor |
| 431 | ERR_NONICKNAMEGIVEN | NICK sin argumento |
| 433 | ERR_NICKNAMEINUSE | Ese nickname ya lo usa otro cliente |
| 441 | ERR_USERNOTINCHANNEL | El usuario objetivo no esta en el canal (KICK) |
| 442 | ERR_NOTONCHANNEL | Tu no estas en ese canal |
| 443 | ERR_USERONCHANNEL | El usuario ya esta en el canal (INVITE innecesario) |
| 451 | ERR_NOTREGISTERED | Intentas usar un comando sin haberte registrado |
| 461 | ERR_NEEDMOREPARAMS | Faltan parametros en el comando |
| 462 | ERR_ALREADYREGISTERED | Intentas hacer USER de nuevo despues de registrarte |
| 464 | ERR_PASSWDMISMATCH | Contrasena incorrecta en PASS |
| 471 | ERR_CHANNELISFULL | Canal lleno, modo +l activo |
| 473 | ERR_INVITEONLYCHAN | Canal solo por invitacion, modo +i activo |
| 475 | ERR_BADCHANNELKEY | Clave incorrecta, modo +k activo |
| 482 | ERR_CHANOPRIVSNEEDED | No eres operador del canal |

---

## 16. Conceptos clave de C++98 usados

### std::map (contenedor asociativo)

```cpp
std::map<int, Client> _clients;         // Clave: int (fd), Valor: Client
std::map<std::string, Channel> _channels; // Clave: string (nombre), Valor: Channel
```

Un `std::map` es un contenedor que almacena pares clave-valor, ordenados por clave. Internamente usa un arbol rojo-negro, asi que las busquedas son O(log n).

```cpp
// Insertar
_clients.insert(std::make_pair(fd, Client(fd)));

// Acceder
Client &c = _clients[fd];  // Accede o crea si no existe

// Buscar sin crear
if (_clients.find(fd) != _clients.end()) { /* existe */ }

// Iterar
for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
    int fd = it->first;           // La clave
    Client &client = it->second;  // El valor
}

// Borrar
_clients.erase(fd);       // Por clave
_clients.erase(iterator); // Por iterador
```

### std::vector (array dinamico)

```cpp
std::vector<struct pollfd> _pollFds;
std::vector<int> _clientsFds;
```

Array que crece automaticamente. Acceso por indice O(1), insercion al final O(1) amortizado, busqueda O(n).

```cpp
vec.push_back(elemento);           // Anadir al final
vec.erase(iterator);               // Borrar por iterador
vec.size();                         // Cuantos elementos hay
vec.empty();                        // Esta vacio?
```

### std::istringstream (parseo de strings)

```cpp
std::istringstream iss("NICK alice");
std::string cmd, nick;
iss >> cmd;    // cmd = "NICK"
iss >> nick;   // nick = "alice"

// Para leer el resto de la linea (incluidos espacios):
std::string rest;
std::getline(iss, rest);  // rest = " todo el texto restante"
```

Permite tratar un `std::string` como si fuera un flujo de entrada (como `std::cin`). El operador `>>` extrae palabras separadas por espacios. `std::getline` extrae hasta el fin de linea.

### std::find y std::remove (de <algorithm>)

```cpp
// Buscar un elemento en un vector:
std::find(vec.begin(), vec.end(), valor);
// Devuelve iterador al elemento, o vec.end() si no lo encuentra

// Patron erase-remove (borrar por valor):
vec.erase(std::remove(vec.begin(), vec.end(), valor), vec.end());
```

### Excepciones y herencia

```cpp
class SocketException : public std::exception {
public:
    const char* what() const throw() { return "Socket creation failed"; }
};

// Lanzar:
throw SocketException();

// Capturar:
try { ... }
catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
}
```

`std::exception` es la clase base de todas las excepciones estandar. `what()` es un metodo virtual que devuelve una descripcion del error. Al heredar de `std::exception`, nuestras excepciones se pueden capturar con `catch(std::exception &e)`.

### Punteros y NULL

```cpp
Client* getClientByNick(const std::string &nick);

Client *target = getClientByNick("alice");
if (target == NULL) {
    // "alice" no existe
} else {
    target->getFd();  // Acceder al fd de alice
}
```

En C++98 no existe `nullptr` (eso es C++11). Se usa `NULL` (que es una macro que vale 0) para indicar un puntero que no apunta a nada.

### Listas de inicializacion

```cpp
Server::Server(int port, const std::string &password)
    : _port(port), _password(password) {   // Lista de inicializacion
    setupServerSocket();                    // Cuerpo del constructor
}
```

La sintaxis `: atributo(valor)` inicializa los atributos ANTES de que se ejecute el cuerpo del constructor. Es mas eficiente que asignar dentro del cuerpo (sobre todo para strings y objetos complejos, porque evita una construccion por defecto seguida de una asignacion).

### Operaciones de bits (bitwise)

```cpp
pfd.events |= POLLOUT;     // OR: activa el bit de POLLOUT
pfd.events &= ~POLLOUT;    // AND NOT: desactiva el bit de POLLOUT
pfd.revents & POLLIN        // AND: comprueba si el bit de POLLIN esta activo
```

Los eventos de `poll` son **bitmasks**: cada bit del short representa un evento. Con operaciones de bits puedes activar, desactivar y comprobar flags individuales sin afectar al resto.

---

## 17. Patrones de diseno utilizados

### 17.1 Patron Reactor (Event Loop)

El servidor usa un unico hilo con un bucle de eventos basado en `poll()`. En lugar de crear un hilo por conexion (costoso en recursos y complejo de sincronizar), un solo hilo gestiona todas las conexiones comprobando cuales tienen actividad.

```
[poll() espera] -> [evento detectado] -> [despachar al handler] -> [volver a poll()]
```

**Ventaja:** Simple, eficiente, sin problemas de concurrencia.
**Limitacion:** Si un comando tarda mucho en procesarse, bloquea temporalmente a todos los demas clientes. Para un servidor IRC esto no es problema porque todos los comandos son rapidos.

### 17.2 Buffered I/O

Tanto la lectura como la escritura usan buffers intermedios. Esto maneja correctamente:
- **Fragmentacion TCP** (buffer de entrada): datos parciales se acumulan hasta tener una linea completa.
- **Contrapresion de red** (buffer de salida): si el sistema no puede enviar todo de golpe, se reintenta en la siguiente iteracion.

### 17.3 Mediator (Server como intermediario)

`Client` y `Channel` no se comunican entre si directamente. Toda la comunicacion pasa por `Server`, que actua como intermediario. Esto simplifica las dependencias: `Client` y `Channel` son clases independientes y reutilizables. Ninguna sabe de la existencia de la otra.

### 17.4 Command Dispatcher

`processCommand()` actua como un despachador que traduce strings de texto en llamadas a funciones. Cada comando IRC tiene su propio metodo handler. Es una version simplificada del patron Command.

---

## 18. Flujo completo de una conexion (ejemplo)

Ejemplo paso a paso de lo que ocurre cuando un usuario se conecta y chatea:

```
1. CONEXION TCP
   El usuario ejecuta: nc 127.0.0.1 6667
   -> El OS realiza el TCP handshake con nuestro servidor.
   -> poll() detecta POLLIN en _serverSocket.
   -> handleNewConnection():
      - accept() devuelve fd=5 (ejemplo).
      - fcntl(5, F_SETFL, O_NONBLOCK).
      - Crea pollfd{fd=5, events=POLLIN}.
      - Crea Client(5) en _clients.

2. PASS
   El usuario envia: PASS secreto123\r\n
   -> poll() detecta POLLIN en fd=5.
   -> handleClientRead(5):
      - recv() lee "PASS secreto123\r\n".
      - appendInBuffer -> extractLine -> "PASS secreto123".
      - processCommand -> cmdPass().
      - Compara "secreto123" con _password. Coincide -> _hasPassed = true.

3. NICK
   El usuario envia: NICK alice\r\n
   -> cmdNick(): verifica que "alice" no existe.
   -> _nickname = "alice". checkRegistration(): falta username.

4. USER
   El usuario envia: USER alice 0 * :Alice Smith\r\n
   -> cmdUser(): _username = "alice". checkRegistration():
      _hasPassed=true, _nickname="alice", _username="alice" -> _isRegistered = true.
   -> Envia: ":server 001 alice :Welcome to the ft_irc network"

5. JOIN
   El usuario envia: JOIN #general\r\n
   -> cmdJoin(): "#general" no existe en _channels.
   -> Crea Channel("#general").
   -> addOperator(5): alice es la primera, asi que es operadora.
   -> addClient(5).
   -> Broadcast: ":alice JOIN :#general" a todos en #general (solo alice por ahora).

6. CHATEAR
   Otro usuario (bob, fd=6) ya esta en #general.
   Alice envia: PRIVMSG #general :Hola a todos!\r\n
   -> cmdPrivmsg(): #general existe y alice es miembro.
   -> broadcastToChannel: ":alice PRIVMSG #general :Hola a todos!" a fd=6 (bob).
   -> alice NO recibe su propio mensaje (senderFd = 5, se excluye).

7. DESCONEXION
   Alice cierra netcat (Ctrl+C).
   -> poll() detecta POLLHUP o recv() devuelve 0.
   -> removeClient(5):
      - Quita fd=5 de _pollFds.
      - Quita fd=5 de todos los canales.
      - Si #general queda vacio, borra el canal.
      - Borra Client de _clients.
      - close(5).
```

---

## 19. Resumen visual de la arquitectura

```
                          +--------------------------------------+
     Clientes IRC         |            SERVIDOR                  |
     (irssi, nc, etc)     |                                      |
                          |   +----------------------------+     |
   [Cliente A] <--TCP-->  |   |       poll() loop          |     |
   (fd=5)                 |   |                            |     |
                          |   | POLLIN en serverFd:        |     |
   [Cliente B] <--TCP-->  |   |   -> handleNewConnection() |     |
   (fd=6)                 |   |                            |     |
                          |   | POLLIN en clientFd:        |     |
   [Cliente C] <--TCP-->  |   |   -> handleClientRead()    |     |
   (fd=7)                 |   |   -> processCommand()      |     |
                          |   |   -> cmd*()                |     |
                          |   |                            |     |
                          |   | POLLOUT en clientFd:       |     |
                          |   |   -> handleClientWrite()   |     |
                          |   |                            |     |
                          |   | POLLERR/HUP en clientFd:   |     |
                          |   |   -> removeClient()        |     |
                          |   +----------------------------+     |
                          |                                      |
                          |   _clients (map fd -> Client):       |
                          |     5 -> Client{nick="alice"}        |
                          |     6 -> Client{nick="bob"}          |
                          |     7 -> Client{nick="carol"}        |
                          |                                      |
                          |   _channels (map name -> Channel):   |
                          |     "#general" -> Channel{fds=[5,6]} |
                          |     "#random"  -> Channel{fds=[6,7]} |
                          +--------------------------------------+

  Flujo de datos:

  [recv] -> _inBuffer -> extractLine() -> processCommand() -> cmd*()
                                                                 |
                                                          sendToClient()
                                                                 |
                                                     _outBuffer + POLLOUT
                                                                 |
                                                          [send] -> red
```

Cada cliente tiene su propio file descriptor. Los canales almacenan listas de file descriptors de sus miembros. El servidor es el unico que conoce la relacion completa entre FDs, Clients y Channels, y actua como intermediario para toda la comunicacion.
