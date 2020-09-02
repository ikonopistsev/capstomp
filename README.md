[![linux](https://github.com/ikonopistsev/capstomp/workflows/linux/badge.svg)](https://github.com/ikonopistsev/capstomp/actions?query=workflow%3Alinux)

# capstomp

`capstomp` is a [MySQL](https://en.wikipedia.org/wiki/MySQL) user-defined function ([udf](https://dev.mysql.com/doc/extending-mysql/8.0/en/adding-functions.html)) library for sending messages to message brokers like [RabbitMQ](https://en.wikipedia.org/wiki/RabbitMQ) or [Apache ActiveMQ](https://en.wikipedia.org/wiki/Apache_ActiveMQ) using the [STOMP](https://en.wikipedia.org/wiki/Streaming_Text_Oriented_Messaging_Protocol) protocol.

To use with the RabbitMQ, the [STOMP plugin](https://www.rabbitmq.com/stomp.html) is required.

Each method of sending data blocks the DBMS trigger until the data is sent. The first time the method is called, it connects to the message broker and sends data. The next calls to the DBMS trigger use the already established connection.

## Requirements

* C++ compiler (tested with gcc and clang) and cmake build tool.
* [MySQL](http://www.mysql.com/) or forks
* [stomptalk](https://github.com/ikonopistsev/stomptalk) STOMP protocol parser library
* [stompconn](https://github.com/ikonopistsev/stompconn) simple STOMP connector
* [libevent](https://github.com/libevent/libevent) event notification library
* [RabbitMQ STOMP plugin](https://www.rabbitmq.com/stomp.html) RabbitMQ STOMP protocol support

## Building

```
git clone --recurse-submodules https://github.com/ikonopistsev/capstomp.git
```

cmake build
```
$ mkdir b && cd b
$ cmake ..
# ccmake .. (if needed)
$ make
```

copy libcapstomp.so to mysql pugins directory (usaly to /usr/lib/mysql/plugin/ or same) then import 10 methods
```
CREATE FUNCTION capstomp_json01 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json02 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json03 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json04 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json05 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json06 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json07 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json08 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json09 RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json10 RETURNS INTEGER SONAME 'libcapstomp.so';
```

## Example

### Hello, World!

Publishes a json `['Hello, World!']` to the `udf` exchange on `localhost:61613` with a routing key of `test` as the user `guest` with the password `guest`. Upon success, the message size is returned.
```
mysql> SELECT capstomp_json01(61613, 'guest', 'guest', '', '/exchange/udf/test', json_array('Hello, World!'));
```
this example works with [MySQL json functions](https://dev.mysql.com/doc/refman/8.0/en/json-functions.html)

You may use any json generator for MySQL. I use [my own](https://github.com/ikonopistsev/capjs) :)
```
mysql> SELECT capstomp_json01(61613, 'guest', 'guest', '', '/exchange/udf/test', jsarr('Hello, World!'));
```

## API

### `capstomp_json01(port|"addr:port", user, passcode, vhost, destination, json-data [, stomp-header...])`

Sends a plain text `message` to the given `exchange` on the provided `hostname` and `port` with the supplied `routingKey` as `username` identified by `password`.

#### Parameters

* `port` (integer) or addr:port (string). "localhost:61613" try to use only local connections!
* `destination` (string). [STOMP exchange or queue](https://www.rabbitmq.com/stomp.html#d).
* `json-data` (string). The body of the message (typically json but it may any string).

#### Returns

Upon succes, this function returns a number containing the size of sending data.

`Based on [lib_mysqludf_amqp](https://github.com/ssimicro/lib_mysqludf_amqp)`