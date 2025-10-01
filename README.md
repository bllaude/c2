# Local c2 (Linux, C)

A minimal Command & Control system for Linux written in C, for red teaming and security research. 

## Components
- `controller.c`: Server that accepts commands and receives responses.
- `agent.c`: Client that connects to the controller and executes received commands.

## Usage

### Build
```bash
make
