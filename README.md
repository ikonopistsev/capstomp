[![linux](https://github.com/ikonopistsev/capstomp/workflows/linux/badge.svg)](https://github.com/ikonopistsev/capstomp/actions?query=workflow%3Alinux)

# capstomp

`capstomp` is a [MySQL](https://en.wikipedia.org/wiki/MySQL) user-defined function ([udf](https://dev.mysql.com/doc/extending-mysql/8.0/en/adding-functions.html)) library for sending messages to message brokers like [ActiveMQ Artemis](https://activemq.apache.org/components/artemis/) or [RabbitMQ](https://en.wikipedia.org/wiki/RabbitMQ) using the [STOMP](https://en.wikipedia.org/wiki/Streaming_Text_Oriented_Messaging_Protocol) protocol. Module has very hi performance based on pool of persistent tcp connections.

To use with the RabbitMQ, the [STOMP plugin](https://www.rabbitmq.com/stomp.html) is required.

## Requirements

* C++ compiler (tested with gcc and clang) and cmake build tool.
* [MySQL](http://www.mysql.com/) or forks
* [stomptalk](https://github.com/ikonopistsev/stomptalk) STOMP protocol parser library
* [stompconn](https://github.com/ikonopistsev/stompconn) simple STOMP connector
* [libevent](https://github.com/libevent/libevent) event notification library
* [ActiveMQ Artemis](http://activemq.apache.org/components/artemis/) with native STOMP protocol support
* [OR] [RabbitMQ with STOMP plugin](https://www.rabbitmq.com/stomp.html) for protocol support

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

### Build with static linked libevent

```
...
$ cmake -DCMAKE_BUILD_TYPE=Release -DCAPSTOMP_STATIC_LIBEVENT=ON ..
...
```

Add `-DCAPSTOMP_HAVE_MY_BOOL=ON` if `my_bool` type is present in `mysql.h`

### Build on CentOS 7

1. You need to install cmake3 from [EPEL](https://fedoraproject.org/wiki/EPEL) repository

```
$ yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
$ yum update
$ yum install cmake3
```

2. you need install new C++ compiller with c++17 support

First, install the CentOS SCL release file. It is part of the CentOS extras repository and can be installed by running the following command:

```
$ sudo yum install centos-release-scl
```

In this example, we’ll install the Developer Toolset version 9. To do so type the following command on your CentOS 7 terminal:

```
$ sudo yum install devtoolset-9
```

To access GCC version 9, you need to launch a new shell instance using the Software Collection scl tool:

```
$ scl enable devtoolset-9 bash
```

Now if you check the GCC version, you’ll notice that GCC 9 is the default version in your current shell:

```
$ gcc --version

gcc (GCC) 9.3.1 20200408 (Red Hat 9.3.1-2)
```

3. install libevent and mysql development files

```
$ yum install libevent-dev mariadb-devel
```

4. If you want make an rpm package you need to install rpm-build

```
$ yum install -y rpm-build
```

5. build

in capstomp directory 

```
$ mkdir b && cd b
```

build static dependencies 

```
$ cmake3 -DCMAKE_BUILD_TYPE=Release -DCAPSTOMP_HAVE_MY_BOOL=ON -DCAPSTOMP_STATIC_STDCPP=ON -DCPACK_GENERATOR="RPM" ..
```

check the dependencies

```
$ ldd libcapstomp.so 
        linux-vdso.so.1 =>  (0x00007fff209be000)
        libevent-2.0.so.5 => /lib64/libevent-2.0.so.5 (0x00007f7da60b7000)
        libm.so.6 => /lib64/libm.so.6 (0x00007f7da5db5000)
        libc.so.6 => /lib64/libc.so.6 (0x00007f7da59e7000)
        /lib64/ld-linux-x86-64.so.2 (0x00007f7da62ff000)
        libpthread.so.0 => /lib64/libpthread.so.0 (0x00007f7da57cb000)
```

### Installation 

copy `libcapstomp.so` to mysql pugins directory (usually to `/usr/lib/mysql/plugin` or same) then import methods
```
CREATE FUNCTION capstomp RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_status RETURNS STRING SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_store_erase RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_read_timeout RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_max_pool_count RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_max_pool_sockets RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_pool_sockets RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_enable RETURNS integer SONAME 'libcapstomp.so';
```

> Discription based on [lib_mysqludf_amqp](https://github.com/ssimicro/lib_mysqludf_amqp)

