[![linux](https://github.com/ikonopistsev/capstomp/workflows/linux/badge.svg)](https://github.com/ikonopistsev/capstomp/actions?query=workflow%3Alinux)

# capstomp

`capstomp` is a [MySQL](https://en.wikipedia.org/wiki/MySQL) user-defined function ([udf](https://dev.mysql.com/doc/extending-mysql/8.0/en/adding-functions.html)) library for sending messages to message brokers like [RabbitMQ](https://en.wikipedia.org/wiki/RabbitMQ) or [Apache ActiveMQ](https://en.wikipedia.org/wiki/Apache_ActiveMQ) using the [STOMP](https://en.wikipedia.org/wiki/Streaming_Text_Oriented_Messaging_Protocol) protocol.

To use with the RabbitMQ, the [STOMP plugin](https://www.rabbitmq.com/stomp.html) is required.

Each call of method blocks the DBMS trigger until the data is sent. The first time the method is called, it connects to the message broker and sends data. The next calls to the DBMS trigger use the already established connection.

## Requirements

* C++ compiler (tested with gcc and clang) and cmake build tool.
* [MySQL](http://www.mysql.com/) or forks
* [stomptalk](https://github.com/ikonopistsev/stomptalk) STOMP protocol parser library
* [stompconn](https://github.com/ikonopistsev/stompconn) simple STOMP connector
* [libevent](https://github.com/libevent/libevent) event notification library
* [RabbitMQ STOMP plugin](https://www.rabbitmq.com/stomp.html) RabbitMQ STOMP protocol support

## Building

Build with cmake and system libevent

```
git clone --recurse-submodules https://github.com/ikonopistsev/capstomp.git
$ cd capstomp
$ mkdir b && cd b
$ cmake -DCMAKE_BUILD_TYPE=Release ..
# ccmake .. (if needed)
$ make
```

Build with static linked libevent

```
...
$ cmake -DCMAKE_BUILD_TYPE=Release -DCAPSTOMP_STATIC_LIBEVENT=ON ..
...
```

Add `-DCAPSTOMP_HAVE_MY_BOOL=ON` if `my_bool` type is present in `mysql.h`

copy `libcapstomp.so` to mysql pugins directory (usaly to `/usr/lib/mysql/plugin` or same) then import methods
```
CREATE FUNCTION capstomp RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json RETURNS INTEGER SONAME 'libcapstomp.so';
```

## Example

### Hello, World!

Publishes a json `['Hello, World!']` to the `udf` exchange on `localhost:61613` with a routing key of `test` as the user `guest` with the password `guest` and vhost `123`. Upon success, the message size is returned.
```
mysql> SELECT capstomp_json('stomp://guest:guest@localhost/123#/exchange/udf', 'test', json_array('Hello, World!'));
```
this example works with [MySQL json functions](https://dev.mysql.com/doc/refman/8.0/en/json-functions.html)

You may use any json generator for MySQL. I use [my own](https://github.com/ikonopistsev/capjs) :)
```
mysql> SELECT capstomp_json('stomp://guest:guest@localhost:61613/123#/exchange/udf', 'test', jsarr('Hello, World!'));
```

## API

### `capstomp(uri, routing-key, json-data [, stomp-header...])`

Sends a `json-data` to the given `destination` on the provided `uri`.

#### Parameters

* `uri` (string). "stomp://guest:guest@localhost/vhost#/stomp_destination/name" [STOMP destination](https://www.rabbitmq.com/stomp.html#d).
* `routing-key` (string). routing key for exchanges, or empty '' [STOMP exchange destination](https://www.rabbitmq.com/stomp.html#d.ed).
* `json-data` (string). The body of the message (typically json but it may any string).
* `stomp-header` (mostly string). `round(unix_timestamp(now(4))*1000) as 'timestamp'` will add to STOMP header `timestamp=1599081164296`.

#### Returns

Upon succes, this function returns a number containing the size of sending data.

### `capstomp_json(uri, routing-key, json-data [, stomp-header...])`

Same as `capstomp` but it add `content-type=application/json` header to each message.

> Discription based on [lib_mysqludf_amqp](https://github.com/ssimicro/lib_mysqludf_amqp)