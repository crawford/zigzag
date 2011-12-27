#Zigzag#
This daemon acts as a gateway between a standard computer network and a
wireless zigbee network. Clients attach to zigzag and use the connection
to send and receive messages to end points on the zigbee network.

##Protocol##
Commands are sent from the client to the server. All commands begin with the
frame start character (0xA5). All commands from the client will receive a
response from the server.

###Commands###
####Send####
Sends a message to the specified zigbee. This operation is asynchronous, so a
message id is provided by the client. When the operation completes, the server
will send a response to the client using the same message id. The message
length needs to be at least 1 byte long (duh) and must be less then about 100
bytes (the exact amount is dependant on hardware). If the message is too long,
the call will fail.

<pre>
+--------+---------+-------------+--------------+------------------+----------------+
| <0xA5> | 'send'  | <zigbee id> | <message id> | <message length> |   <message>    |
+--------+---------+-------------+--------------+------------------+----------------+
| 1 Byte | 4 Bytes |   8 Bytes   |    1 byte    |      1 Byte      | 1 - ~100 bytes |
+--------+---------+-------------+--------------+------------------+----------------+
</pre>

####Subscribe####
Subscribes the client to messages from the specified zigbee. This operation is
synchronous. There is no warning or error if the specified zigbee doesn't
exist (so be careful).

<pre>
+--------+---------+-------------+
| <0xA5> |  'sub'  | <zigbee id> |
+--------+---------+-------------+
| 1 Byte | 3 Bytes |   8 Bytes   |
+--------+---------+-------------+
</pre>

###Response###
Sent from the server as a result of a 'send' operation completing. The
specified message id is returned as well as the operation's result. The
meaning of the result values are defined in the headers.

<pre>
+--------+---------+--------------+----------+
| <0xA5> |  'res'  | <message id> | <result> |
+--------+---------+--------------+----------+
| 1 Byte | 3 Bytes |    1 Byte    |  1 Byte  |
+--------+---------+--------------+----------+
</pre>

