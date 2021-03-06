/* upstart
 *
 * wire.c - control socket wire protocol
 *
 * Copyright © 2007 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <arpa/inet.h>

#include <stdarg.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/logging.h>
#include <nih/io.h>

#include <upstart/message.h>
#include <upstart/wire.h>


/**
 * MAGIC:
 *
 * Magic string that is placed on the front of all messages.  In theory, we
 * don't need this; as we strongly guard against invalid messages anyway -
 * however it's a useful check that both sides are at least agreeing in
 * principal to communicate.
 **/
#define MAGIC "upstart\n"


/**
 * upstart_push_int:
 * @message: message to write to,
 * @value: value to write.
 *
 * Write an integer @value to the @message given.
 *
 * Integers are transmitted across the wire as a single 'i' byte indicating
 * an integer followed by a signed 32-bit value, in network byte order.
 *
 * Failure to allocate memory can result in the buffer contents containing
 * part of a message; if this happens, the entire message buffer should be
 * discarded.
 *
 * Returns: zero on success, negative value if insufficient memory.
 **/
int
upstart_push_int (NihIoMessage *message,
		  int           value)
{
	int32_t wire_value;

	nih_assert (message != NULL);

	if (nih_io_buffer_push (message->data, "i", 1) < 0)
		return -1;

	wire_value = ntohl (value);

	return nih_io_buffer_push (message->data, (const char *)&wire_value,
				   sizeof (wire_value));
}

/**
 * upstart_pop_int:
 * @message: message to read from,
 * @value: pointer to write to.
 *
 * Read an integer value from the @message given, storing the value
 * in the integer pointed to by @value and removing it from the message.
 *
 * Integers are transmitted across the wire as a single 'i' byte indicating
 * an integer followed by a signed 32-bit value, in network byte order.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_pop_int (NihIoMessage *message,
		 int          *value)
{
	int32_t wire_value;

	nih_assert (message != NULL);
	nih_assert (value != NULL);

	if (message->data->len < (sizeof (wire_value) + 1))
		return -1;

	/* Extract the type byte first. */
	if (message->data->buf[0] != 'i')
		return -1;


	memcpy (&wire_value, message->data->buf + 1, sizeof (wire_value));
	nih_io_buffer_shrink (message->data, sizeof (wire_value) + 1);

	wire_value = ntohl (wire_value);
	if ((wire_value < INT_MIN) || (wire_value > INT_MAX))
		return -1;

	*value = (int)wire_value;

	return 0;
}


/**
 * upstart_push_unsigned:
 * @message: message to write to,
 * @value: value to write.
 *
 * Write an unsigned @value to the @message given.
 *
 * Unsigneds are transmitted across the wire as a single 'u' byte indicating
 * an unsigned followed by a 32-bit value, in network byte order.
 *
 * Failure to allocate memory can result in the buffer contents containing
 * part of a message; if this happens, the entire message buffer should be
 * discarded.
 *
 * Returns: zero on success, negative value if insufficient memory.
 **/
int
upstart_push_unsigned (NihIoMessage *message,
		       unsigned int  value)
{
	uint32_t wire_value;

	nih_assert (message != NULL);

	if (nih_io_buffer_push (message->data, "u", 1) < 0)
		return -1;

	wire_value = ntohl (value);

	return nih_io_buffer_push (message->data, (const char *)&wire_value,
				   sizeof (wire_value));
}

/**
 * upstart_pop_unsigned:
 * @message: message to read from,
 * @value: pointer to write to.
 *
 * Read an unsigned value from the @message given, storing the value
 * in the variable pointed to by @value and removing it from the message.
 *
 * Unsigneds are transmitted across the wire as a single 'u' byte indicating
 * an unsigned followed by a 32-bit value, in network byte order.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_pop_unsigned (NihIoMessage *message,
		      unsigned int *value)
{
	uint32_t wire_value;

	nih_assert (message != NULL);
	nih_assert (value != NULL);

	if (message->data->len < (sizeof (wire_value) + 1))
		return -1;

	/* Extract the type byte first. */
	if (message->data->buf[0] != 'u')
		return -1;


	memcpy (&wire_value, message->data->buf + 1, sizeof (wire_value));
	nih_io_buffer_shrink (message->data, sizeof (wire_value) + 1);

	wire_value = ntohl (wire_value);
	if (wire_value > UINT_MAX)
		return -1;

	*value = (unsigned int)wire_value;

	return 0;
}


/**
 * upstart_push_string:
 * @message: message to write to,
 * @value: value to write.
 *
 * Write a string @value to the @message given.
 *
 * Strings are transmitted across the wire as a single 's' byte indicating
 * a string followed by an unsigned 32-bit value containing the length and
 * that number of bytes containing the string itself, without any NULL
 * terminator.
 *
 * @value may be an empty string, in which case a zero length is sent
 * with no following bytes; it may also be NULL in which case the special
 * 'S' byte is used instead followed by no length or bytes.
 *
 * Failure to allocate memory can result in the buffer contents containing
 * part of a message; if this happens, the entire message buffer should be
 * discarded.
 *
 * Returns: zero on success, negative value if insufficient memory.
 **/
int
upstart_push_string (NihIoMessage *message,
		     const char   *value)
{
	uint32_t wire_len;

	nih_assert (message != NULL);

	if (nih_io_buffer_push (message->data, value ? "s" : "S", 1) < 0)
		return -1;

	if (! value)
		return 0;

	wire_len = ntohl (value ? strlen (value) : 0xffffffff);
	if (nih_io_buffer_push (message->data, (const char *)&wire_len,
				sizeof (wire_len)) < 0)
		return -1;

	return nih_io_buffer_push (message->data, value, strlen (value));
}

/**
 * upstart_pop_string:
 * @message: message to read from,
 * @parent: parent of new string,
 * @value: pointer to store string.
 *
 * Read a string value from the @message given, allocate the new string
 * with nih_alloc and store it in the variable pointed to by @value,
 * removing it from the message.
 *
 * Strings are transmitted across the wire as an unsigned 32-bit value
 * containing the length, followed by that number of bytes containing the
 * string itself, without any NULL terminator.
 *
 * If the length of the string on the wire is zero, @value will be set
 * to an allocated zero-length string; if the special length 0xffffffff
 * is read, @value will be set to NULL.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_pop_string (NihIoMessage  *message,
		    const void    *parent,
		    char         **value)
{
	uint32_t wire_length;
	size_t   length;

	nih_assert (message != NULL);
	nih_assert (value != NULL);

	/* Extract the type byte first, which tells us whether to return NULL
	 * or read more items.
	 */
	if (message->data->len < 1)
		return -1;

	if (message->data->buf[0] == 'S') {
		nih_io_buffer_shrink (message->data, 1);

		*value = NULL;
		return 0;
	} else if (message->data->buf[0] != 's') {
		return -1;
	}


	if (message->data->len < (sizeof (wire_length) + 1))
		return -1;

	memcpy (&wire_length, message->data->buf + 1, sizeof (wire_length));
	length = ntohl (wire_length);

	/* Allocate the string and copy it out of the buffer */
	if (message->data->len < (sizeof (wire_length) + length + 1))
		return -1;

	NIH_MUST (*value = nih_alloc (parent, length + 1));
	memcpy (*value, message->data->buf + sizeof (wire_length) + 1, length);
	(*value)[length] = '\0';

	nih_io_buffer_shrink (message->data,
			      sizeof (wire_length) + length + 1);

	return 0;
}


/**
 * upstart_push_array:
 * @message: message to write to,
 * @value: value to write.
 *
 * Write a NULL-terminated array of strings @value to the @message given.
 *
 * Arrays are transmitted across the wire as a single 'a' byte indicating
 * an array, followed by one string for each of the elements and a
 * terminating NULL string.
 *
 * @array may be NULL, in which case the special 'A' byte is transmitted
 * instead followed by no strings.
 *
 * Failure to allocate memory can result in the buffer contents containing
 * part of a message; if this happens, the entire message buffer should be
 * discarded.
 *
 * Returns: zero on success, negative value if insufficient memory.
 **/
int
upstart_push_array (NihIoMessage *message,
		    char * const *value)
{
	char * const *elem;

	nih_assert (message != NULL);

	if (nih_io_buffer_push (message->data, value ? "a" : "A", 1) < 0)
		return -1;

	if (! value)
		return 0;

	for (elem = value; elem && *elem; elem++)
		if (upstart_push_string (message, *elem) < 0)
			return -1;

	return upstart_push_string (message, NULL);
}

/**
 * upstart_pop_array:
 * @message: message to write to,
 * @parent: parent of new string,
 * @value: value to write.
 *
 * Read a NULL-terminated array of strings from the @message given, allocate
 * the new array with nih_alloc and store it in the variable pointed to by
 * @value, removing it from the message.
 *
 * Arrays are transmitted across the wire as a single 'a' byte indicating
 * an array, followed by one string for each of the elements and a
 * terminating NULL string.
 *
 * The special 'A' byte may transmitted instead followed by no strings,
 * in which case @value is set to NULL.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_pop_array (NihIoMessage   *message,
		   const void     *parent,
		   char         ***value)
{
	char   *elem;
	size_t  len;

	nih_assert (message != NULL);
	nih_assert (value != NULL);

	/* Extract the type byte first, which tells us whether to return NULL
	 * or read more items.
	 */
	if (message->data->len < 1)
		return -1;

	if (message->data->buf[0] == 'A') {
		nih_io_buffer_shrink (message->data, 1);

		*value = NULL;
		return 0;
	} else if (message->data->buf[0] != 'a') {
		return -1;
	}

	nih_io_buffer_shrink (message->data, 1);


	/* Allocate the array and begin receiving strings. */
	NIH_MUST (*value = nih_str_array_new (parent));
	len = 0;

	do {
		if (upstart_pop_string (message, *value, &elem)) {
			nih_free (*value);
			*value = NULL;
			return -1;
		}

		if (elem)
			NIH_MUST (nih_str_array_addp (value, parent,
						      &len, elem));
	} while (elem);

	return 0;
}


/**
 * upstart_push_header:
 * @message: message to write to,
 * @type: message type to write.
 *
 * Write a header for a @type message to @message given.
 *
 * The message header consists of a "magic" string ("upstart\n") followed
 * by the message type transmitted as a signed 32-bit value in network
 * byte order.
 *
 * Failure to allocate memory can result in the buffer contents containing
 * part of a message; if this happens, the entire message buffer should be
 * discarded.
 *
 * Returns: zero on success, negative value if insufficient memory.
 **/
int
upstart_push_header (NihIoMessage       *message,
		     UpstartMessageType  type)
{
	int32_t wire_type;

	nih_assert (message != NULL);

	if (nih_io_buffer_push (message->data, MAGIC, strlen (MAGIC)))
		return -1;

	wire_type = ntohl (type);
	if (nih_io_buffer_push (message->data, (const char *)&wire_type,
				sizeof (wire_type)) < 0)
		return -1;

	return 0;
}

/**
 * upstart_pop_header:
 * @message: message to read from,
 * @type: pointer to write message type to.
 *
 * Read a message header from the @message given, storing type of message
 * in the variable pointed to by @value and removing the header from the
 * message.
 *
 * The message header consists of a "magic" string ("upstart\n") followed
 * by the message type transmitted as a signed 32-bit value in network
 * byte order.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_pop_header (NihIoMessage       *message,
		    UpstartMessageType *type)
{
	int32_t wire_type;

	nih_assert (message != NULL);
	nih_assert (type != NULL);

	if (message->data->len < (strlen (MAGIC) + sizeof (wire_type)))
		return -1;

	if (memcmp (message->data->buf, MAGIC, strlen (MAGIC)))
		return -1;

	memcpy (&wire_type, message->data->buf + strlen (MAGIC),
		sizeof (wire_type));
	*type = ntohl (wire_type);

	nih_io_buffer_shrink (message->data,
			      strlen (MAGIC) + sizeof (wire_type));

	return 0;
}


/**
 * upstart_push_packv:
 * @message: message to write to,
 * @pack: pack of values,
 * @args: arguments.
 *
 * Write a set of values, as determined by @pack, to the @message given.
 *
 * @pack is a string that indicates the types of @args.
 *  i - int            (written with upstart_push_int)
 *  u - unsigned int   (written with upstart_push_unsigned)
 *  s - const char *   (written with upstart_push_string)
 *  a - char * const * (written with upstart_push_array)
 *
 * Failure to allocate memory can result in the buffer contents containing
 * part of a message; if this happens, the entire message buffer should be
 * discarded.
 *
 * Returns: zero on success, negative value if insufficient memory.
 **/
int
upstart_push_packv (NihIoMessage *message,
		    const char   *pack,
		    va_list       args)
{
	va_list args_copy;

	nih_assert (message != NULL);
	nih_assert (pack != NULL);

	va_copy (args_copy, args);

	for (; *pack; pack++) {
		int ret;

		switch (*pack) {
		case 'i':
			ret = upstart_push_int (
				message, va_arg (args_copy, int));
			break;
		case 'u':
			ret = upstart_push_unsigned (
				message, va_arg (args_copy, unsigned int));
			break;
		case 's':
			ret = upstart_push_string (
				message, va_arg (args_copy, const char *));
			break;
		case 'a':
			ret = upstart_push_array (
				message, va_arg (args_copy, char * const *));
			break;
		default:
			nih_assert_not_reached ();
		}

		if (ret)
			return ret;
	}

	return 0;
}

/**
 * upstart_push_pack:
 * @message: message to write to,
 * @pack: pack of values.
 *
 * Write a set of values, as determined by @pack, to the @message given.
 *
 * @pack is a string that indicates the types of the following arguments:
 *  i - int          - written with upstart_push_int()
 *  u - unsigned int - written with upstart_push_unsigned()
 *  s - const char * - written with upstart_push_string()
 *  a - char * const * (written with upstart_push_array)
 *
 * Failure to allocate memory can result in the buffer contents containing
 * part of a message; if this happens, the entire message buffer should be
 * discarded.
 *
 * Returns: zero on success, negative value if insufficient memory.
 **/
int
upstart_push_pack (NihIoMessage *message,
		   const char   *pack,
		   ...)
{
	va_list args;
	int     ret;

	nih_assert (message != NULL);
	nih_assert (pack != NULL);

	va_start (args, pack);
	ret = upstart_push_packv (message, pack, args);
	va_end (args);

	return ret;
}

/**
 * upstart_pop_pack:
 * @message: message to read from,
 * @parent: parent of new strings,
 * @pack: pack of values,
 * @args: arguments.
 *
 * Read a set of values, as determined by @pack, from the @message given
 * removing them from the message.
 *
 * @pack is a string that indicates the types of @args:
 *  i - int *          - read with upstart_pop_int()
 *  u - unsigned int * - read with upstart_pop_unsigned()
 *  s - char **        - read with upstart_pop_string()
 *  a - char ***       - read with upstart_pop_array()
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_pop_packv (NihIoMessage *message,
		   const void   *parent,
		   const char   *pack,
		   va_list       args)
{
	va_list args_copy;

	nih_assert (message != NULL);
	nih_assert (pack != NULL);

	va_copy (args_copy, args);

	for (; *pack; pack++) {
		int ret;

		switch (*pack) {
		case 'i':
			ret = upstart_pop_int (
				message, va_arg (args_copy, int *));
			break;
		case 'u':
			ret = upstart_pop_unsigned (
				message, va_arg (args_copy, unsigned int *));
			break;
		case 's':
			ret = upstart_pop_string (
				message, parent, va_arg (args_copy, char **));
			break;
		case 'a':
			ret = upstart_pop_array (
				message, parent, va_arg (args_copy, char ***));
			break;
		default:
			nih_assert_not_reached ();
		}

		if (ret)
			return ret;
	}

	return 0;
}

/**
 * upstart_pop_pack:
 * @message: message to read from,
 * @parent: parent of new strings,
 * @pack: pack of values.
 *
 * Read a set of values, as determined by @pack, from the @message given
 * removing them from the message.
 *
 * @pack is a string that indicates the types of the following arguments:
 *  i - int *          - read with upstart_pop_int()
 *  u - unsigned int * - read with upstart_pop_unsigned()
 *  s - char **        - read with upstart_pop_string()
 *  a - char ***       - read with upstart_pop_array()
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_pop_pack (NihIoMessage *message,
		  const void   *parent,
		  const char   *pack,
		  ...)
{
	va_list args;
	int     ret;

	nih_assert (message != NULL);
	nih_assert (pack != NULL);

	va_start (args, pack);
	ret = upstart_pop_packv (message, parent, pack, args);
	va_end (args);

	return ret;
}
