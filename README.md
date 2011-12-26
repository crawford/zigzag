#Zigzag#

##Protocol##

<pre>
+---------+----------+--------------------+
| Command | Argument | Payload (Optional) |
+---------+----------+--------------------+
</pre>

Fields seperated by spaces (three fields maximum).

###Commands###

<pre>
+---------+------------+--------------------------------------------------------------+
| Command | Argument  | Description                                                   |
+---------+------------+--------------------------------------------------------------+
| sub     | zigbee id | Subscribes the client to messages from the specified zigbee   |
| send    | zigbee id | Sends a message (contents of payload) to the specified zigbee |
+---------+-----------+---------------------------------------------------------------+
</pre>

