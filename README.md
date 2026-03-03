*This project has been created as part of the 42 curriculum by omuno.*

# ft_irc: A fully compliant IRC server in C++98

## Description

The `ft_irc` project consists of conceptualizing and creating a fully compliant Internet Relay Chat (IRC) server from scratch using C++ 98. IRC is a widespread text-based communication protocol characterized by real-time messaging, supporting both private direct messages and group interactions within channels.

The primary objective of this project is to gain profound knowledge of network programming and multiplexing. Real-world applications rely heavily on multiplexed I/O to handle thousands of connections simultaneously without dedicating a separate thread to each user. To achieve this, our IRC server is strictly single-threaded, non-blocking, and utilizes a single `poll()` loop to detect incoming events, read data, and dispatch messages dynamically.

**Key Features:**
- Single-threaded core event loop powered by `poll()`.
- Non-blocking Sockets using `fcntl()`.
- Registration phase handling (`PASS`, `NICK`, `USER`).
- Channel handling (`JOIN`, `PRIVMSG`, `NOTICE`, `PART`, `QUIT`).
- Advanced Operator commands (`KICK`, `INVITE`, `TOPIC`, `MODE`).
- Channel Modes support (`+i`, `+t`, `+k`, `+l`, `+o`).
- Resilient to unexpected client drop-outs and partial commands.

---

## Instructions

### Compilation

This project uses a standard `Makefile` to compile. Make sure you have `c++` or `clang++` and standard build tools like `make` installed.

To compile the server, run the following command at the root of the repository:

```bash
make
```

This will produce the `ircserv` executable.

Other available `make` rules:
- `make clean`: Removes object files.
- `make fclean`: Removes object files and the final executable.
- `make re`: Performs `fclean` followed by `make` to completely recompile.

### Execution

The server accepts two arguments: the port it should listen on, and a connection password that clients must provide to join.

```bash
./ircserv <port> <password>
```

**Example:**
```bash
./ircserv 6667 my_secure_password
```

### Usage Examples

Once the server is running, you can connect to it using a standard IRC client (e.g. *Irssi*, *Weechat*, *HexChat*) or directly with `nc` (Netcat) for testing purposes.

**Connecting with Netcat:**

Open a new terminal and connect to the server:
```bash
nc 127.0.0.1 6667
```

Once connected, you must manually send the IRC handshake commands sequentially (press Enter after each, though officially IRC uses `\r\n`):
```text
PASS my_secure_password
NICK my_nick
USER my_user 0 * :Real Name
```
*Note:* The server manages incomplete or fragmented commands correctly, meaning it waits until a newline is sent before treating it as a complete command.

---

## IRC Features & Channel Modes

The server supports creating channels and managing their states through a robust permission system. The first user to join a channel automatically becomes a **Channel Operator**. Operators have elevated privileges and can manage the channel via specific commands.

### Implemented Commands:
- **`JOIN <channel> [key]`**: Join a channel. Creates it if it doesn't exist.
- **`PRIVMSG <target> <text>`**: Send a private message to a user or a channel.
- **`NOTICE <target> <text>`**: Send a notice (similar to PRIVMSG but without automated replies).
- **`KICK <channel> <user> [reason]`**: Eject a user from the channel (Operator only).
- **`INVITE <user> <channel>`**: Invite a user to a channel (Operator only if +i is active).
- **`TOPIC <channel> [topic]`**: View or change the channel's topic. Restricted to Operators if +t is active.
- **`MODE <channel> <+|-mode> [args]`**: Change channel settings (Operator only).

### Channel Modes:
Our server accurately supports the required IRC channel modes via the `MODE` command:
- **`i` (Invite Only):** When enabled (`MODE <channel> +i`), users can only join the channel if they have been explicitly invited by an operator using the `INVITE` command.
- **`t` (Topic Restriction):** When enabled (`MODE <channel> +t`), only channel operators can change the channel's topic. By default, channels are created with this mode active.
- **`k` (Channel Key / Password):** When enabled (`MODE <channel> +k <password>`), users must provide the exact password in their `JOIN` command (`JOIN <channel> <password>`) to enter.
- **`l` (User Limit):** When enabled (`MODE <channel> +l <limit>`), the channel restricts the maximum number of simultaneous users. New `JOIN` attempts will be rejected if the limit is reached.
- **`o` (Operator Privilege):** Allows an existing operator to grant or revoke operator status to another user in the channel (`MODE <channel> +o <user>` or `-o`).

---

## Technical Overview & Manual

This section acts as a detailed manual explaining the internal architecture, classes, and functions that make up the `ft_irc` application.

### Entry Point (`main.cpp`)

The executable starts its lifecycle in `main.cpp`. The core responsibility of the `main(int argc, char **argv)` function is safely booting up the environment.
- **Argument Validation:** It first checks if exactly two arguments (`port` and `password`) are provided. It ensures the port is a valid numeric value within the acceptable TCP range (1-65535) and that the password is not empty.
- **Server Initialization:** Once arguments are validated, it instantiates the central `Server` object.
- **Exception Handling:** The instantiation and the main execution block (`server.run()`) are wrapped in a `try-catch` block to gracefully capture and report any unexpected, low-level OS networking exceptions like socket creation failures or bind addresses already in use, effectively preventing abrupt crashes (Segmentation Faults or unhandled aborts).

### Core Architecture: The `Server` Class

The `Server` class acts as the main orchestrator, managing connectivity, the core event loop (multiplexing), and routing instructions between clients and channels.

#### Member Variables
- `int _port` / `std::string _password`: Stores the connection parameters provided upon execution.
- `int _serverSocket`: The master file descriptor returned by `socket()`. Through this socket, the kernel listens for new TCP handshakes.
- `std::vector<struct pollfd> _pollFds`: The lifeblood of our multiplexer. Every time there is a new connection, its FD and the events we want to listen for (e.g., `POLLIN`) are appended here to be watched by `poll()`.
- `std::map<int, Client> _clients`: A relational map pairing an active file descriptor integer to its corresponding `Client` instance, acting as the state manager for connected users.
- `std::map<std::string, Channel> _channels`: Maintains the server's channels, matching string names (e.g., "#general") to `Channel` instances.

#### Foundational Network Methods
- **`void setupServerSocket()`**: Creates the `AF_INET`, `SOCK_STREAM` server socket. Uses `setsockopt` with `SO_REUSEADDR` to bypass OS timeouts after closing the server. Critically, it calls `fcntl(_serverSocket, F_SETFL, O_NONBLOCK)` to make it non-blocking. It then uses `bind()` and `listen()` to expose it to the network.
- **`void run()`**: The infinite `while(true)` event loop containing our **single allowed `poll()` call**. If `poll()` intercepts events:
  - `POLLIN` on the server socket: triggers `handleNewConnection()`.
  - `POLLIN` on a client socket: triggers `handleClientRead(fd)`.
  - `POLLOUT` on a client socket: triggers `handleClientWrite(fd)`.
  - Errors (`POLLERR`...): triggers `removeClient(fd)`.
- **`void handleNewConnection()`**: Invokes `accept()`, generating a new local FD for the newly arrived client. It forces this new socket to be entirely non-blocking with `fcntl()`, bundles it into a `pollfd` structure, adds it to `_pollFds`, and creates an entry in the `_clients` map.
- **`void handleClientRead(int fd)` / `handleClientWrite(int fd)`**: Receives (`recv()`) or transmits (`send()`) data to network streams without blocking. Fragments of data are deposited into buffers inside the connected `Client` object until fully ready to be processed or flushed.

#### Command Processing
- **`void processCommand()`**: Extracts the primary string (the command like `NICK`, `JOIN`) and distributes the workload using a branching `if-else` map targeting methods like `cmdKick`, `cmdJoin`, etc. It enforces registration: if `Client::isRegistered()` is false, any command other than PASS, NICK, and USER is hard-denied.
- **`void broadcastToChannel()`**: A helper traversing the `_clientsFds` of a specific channel, queuing identical strings into the out-buffers of multiple users.

### Client Management: The `Client` Class

The `Client` class encapsulates user-specific network and protocol states.

#### Member Variables
- `int _fd`: The file descriptor to communicate with the user.
- `std::string _nickname` / `std::string _username`: The user credentials for the chat room context.
- `bool _hasPassed` / `bool _isRegistered`: State flags. First, `_hasPassed` turns true upon a successful `PASS`. Then, `checkRegistration()` triggers `_isRegistered` once the nick and user are properly provided.
- `std::string _inBuffer` / `std::string _outBuffer`: The lifesavers against split TCP packets and `ctrl+Z` freezing scenarios.

#### Vital Buffer Methods
- **`void appendInBuffer()` / `bool extractLine()`**: By leveraging `std::string::find("\n")`, we ensure `ircserv` only retrieves fully constructed lines ending in `\r\n` or `\n`. Incomplete commands are safely stored in `_inBuffer` until the next burst arrives.
- **`void appendOutBuffer()` / `void eraseOutBuffer()`**: Commands ready to be answered (like a Welcome message or channel text) are concatenated here. Later, `handleClientWrite()` pushes this buffer out onto the physical socket when `POLLOUT` says the OS is ready.

### Organization & Isolation: The `Channel` Class

The `Channel` class embodies a private or public chat enclave on our server. It manages the isolated transmission of strings between specific groups of client FDs and applies permission filters.

#### Member Variables
- `std::string _name`: The human-readable hashtag name of the channel.
- `std::string _topic`: The current subject description that users see upon joining.
- `std::vector<int> _clientsFds` / `_operatorsFds`: Essential vectors. While all participants exist in `_clientsFds`, a select few reside in `_operatorsFds`, enjoying administrative authority.
- `bool _inviteOnly`, `bool _topicRestricted`, `std::string _password`, `size_t _userLimit`: Mode indicators managed by the operators through the `MODE` command. 
- `std::vector<int> _invitedFds`: A whitelist vector used when `+i` (Invite Only) is active. Only FDs tracked here can bypass the lock.

#### Operational Methods
- **`void addClient(int fd)` / `removeClient(int fd)`**: Manages subscriptions, safely destroying an operator's privileges if they drop out while the channel is still alive.
- **`bool isOperator(int fd)` / `hasClient(int fd)`**: Securely iterates (`std::find`) across their respective vectors, providing verification tools for `Server` instructions (checking if someone typing `KICK` holds the right to do so).
- **Mode Accessors (e.g., `setInviteOnly()`, `setPassword()`)**: Encapsulated state changers triggered directly by the runtime when parsing valid arguments off a `MODE` command packet.
