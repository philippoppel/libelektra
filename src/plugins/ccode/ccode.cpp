/**
 * @file
 *
 * @brief
 *
 * @copyright BSD License (see LICENSE.md or https://www.libelektra.org)
 */

#include "ccode.hpp"


#include <kdb.h>
#include <kdb.hpp>
#include <kdblogger.h>

using namespace ckdb;
using CppKey = kdb::Key;
using CppKeySet = kdb::KeySet;

using std::max;
using std::string;

namespace
{

/**
 * @brief Cast a character to an unsigned character.
 *
 * @param character This parameter specifies the character this function casts to an unsigned value.
 *
 * @return A unsigned character corresponding to the given argument
 */
inline constexpr unsigned char operator"" _uc (char character) noexcept
{
	return static_cast<unsigned char> (character);
}

/**
 * @brief This function maps hex characters to integer numbers.
 *
 * @pre The specified character has to be between
 *
 *      - `'0'`–`'9'`,
 *      - `'a'`-`'f'`, or
 *      - `'A'`-`'F'`
 *
 *     .
 *
 * @param character This argument specifies the (hexadecimal) character this function converts.
 *
 * @return An integer number between `0` and `15` if the precondition is valid or `0` otherwise
 */
inline int elektraHexcodeConvFromHex (char character)
{
	if (character >= '0' && character <= '9') return character - '0';
	if (character >= 'a' && character <= 'f') return character - 'a' + 10;
	if (character >= 'A' && character <= 'F') return character - 'A' + 10;

	return 0; /* Unknown escape char */
}

/**
 * @brief This function returns a key set containing the contract of this plugin.
 *
 * @return A contract describing the functionality of this plugin.
 */
inline KeySet * contract (void)
{
	return ksNew (30, keyNew ("system/elektra/modules/ccode", KEY_VALUE, "ccode plugin waits for your orders", KEY_END),
		      keyNew ("system/elektra/modules/ccode/exports", KEY_END),
		      keyNew ("system/elektra/modules/ccode/exports/open", KEY_FUNC, elektraCcodeOpen, KEY_END),
		      keyNew ("system/elektra/modules/ccode/exports/close", KEY_FUNC, elektraCcodeClose, KEY_END),
		      keyNew ("system/elektra/modules/ccode/exports/get", KEY_FUNC, elektraCcodeGet, KEY_END),
		      keyNew ("system/elektra/modules/ccode/exports/set", KEY_FUNC, elektraCcodeSet, KEY_END),
#include "readme_ccode.c"
		      keyNew ("system/elektra/modules/ccode/infos/version", KEY_VALUE, PLUGINVERSION, KEY_END), KS_END);
}

/**
 * @brief Retrieve the mapping data and initialize the buffer if it is empty.
 *
 * @return The mapping data stored by this plugin.
 */
CCodeData * retrieveMapping (Plugin * handle)
{
	CCodeData * const mapping = static_cast<CCodeData *> (elektraPluginGetData (handle));
	if (!mapping->buffer)
	{
		mapping->bufferSize = 1000;
		mapping->buffer = new unsigned char[mapping->bufferSize];
	}
	return mapping;
}

/**
 * @brief This function sets default values for the encoding and decoding character mapping.
 *
 * @param mapping This parameter stores the encoding and decoding table this function fills with default values.
 */
void setDefaultConfig (CCodeData * const mapping)
{
	unsigned char pairs[][2] = { { '\b'_uc, 'b'_uc }, { '\t'_uc, 't'_uc }, { '\n'_uc, 'n'_uc },  { '\v'_uc, 'v'_uc },
				     { '\f'_uc, 'f'_uc }, { '\r'_uc, 'r'_uc }, { '\\'_uc, '\\'_uc }, { '\''_uc, '\''_uc },
				     { '\"'_uc, '"'_uc }, { '\0'_uc, '0'_uc } };

	for (size_t pair = 0; pair < sizeof (pairs) / sizeof (pairs[0]); pair++)
	{
		unsigned char character = pairs[pair][0];
		unsigned char replacement = pairs[pair][1];

		mapping->encode[character] = replacement;
		mapping->decode[replacement] = character;
	}
}

/**
 * @brief This function sets values for the encoding and decoding character mapping.
 *
 * @param mapping This parameter stores the encoding and decoding table this function fills with the values specified in `config`.
 * @param config This key set stores the character mappings this function stores inside `mappings`.
 * @param root This key stores the root key for the character mapping stored in `config`.
 */
void readConfig (CCodeData * const mapping, KeySet * const config, Key const * const root)
{
	Key const * key = 0;
	while ((key = ksNext (config)) != 0)
	{
		/* Ignore keys that are not directly below the config root key or have an incorrect size */
		if (keyRel (root, key) != 1 || keyGetBaseNameSize (key) != 3 || keyGetValueSize (key) != 3) continue;

		int character = elektraHexcodeConvFromHex (keyBaseName (key)[1]);
		character += elektraHexcodeConvFromHex (keyBaseName (key)[0]) * 16;

		int replacement = elektraHexcodeConvFromHex (keyString (key)[1]);
		replacement += elektraHexcodeConvFromHex (keyString (key)[0]) * 16;

		/* Hexencode this character! */
		mapping->encode[character & 255] = replacement;
		mapping->decode[replacement & 255] = character;
	}
}

/**
 * @brief This function replaces escaped characters in a string with unescaped characters.
 *
 * The function stores the unescaped result in `mapping->buffer`.
 *
 * @pre The variable `mapping->buffer` needs to be as large as `size`.
 *
 * @param value This string stores the string this function decodes.
 * @param size This number specifies the size of `value` including the trailing `'\0'` character.
 * @param mapping This variable stores the buffer and the character mapping this function uses to decode `value`.
 *
 * @return The size of the decoded string including the trailing `'\0'` character
 */
size_t decode (char const * const value, size_t const size, CCodeData * mapping)
{
	size_t out = 0;
	for (size_t in = 0; in < size - 1; ++in)
	{
		unsigned char character = value[in];

		if (character == mapping->escape)
		{
			++in; /* Advance twice */
			character = value[in];

			mapping->buffer[out] = mapping->decode[character & 255];
		}
		else
		{
			mapping->buffer[out] = character;
		}
		++out; /* Only one char is written */
	}

	mapping->buffer[out] = 0; // null termination for keyString()
	return out + 1;
}

/**
 * @brief This function replaces unescaped characters in a string with escaped characters.
 *
 * The function stores the escaped result value in `mapping->buffer`.
 *
 * @pre The variable `mapping->buffer` needs to be twice as large as size.
 *
 * @param value This variable stores the string this function escapes.
 * @param size This number specifies the size of `value` including the trailing `'\0'` character.
 * @param mapping This variable stores the buffer and the character mapping this function uses to encode `value`.
 *
 * @return The size of the encoded string including the trailing `'\0'` character
 */
size_t encode (char const * const value, size_t const size, CCodeData * mapping)
{
	size_t out = 0;
	for (size_t in = 0; in < size - 1; ++in)
	{
		unsigned char character = value[in];

		if (mapping->encode[character])
		{
			mapping->buffer[out + 1] = mapping->encode[character];
			// Escape char
			mapping->buffer[out] = mapping->escape;
			out += 2;
		}
		else
		{
			// just copy one character
			mapping->buffer[out] = value[in];
			// advance out cursor
			out++;
		}
	}

	mapping->buffer[out] = 0; // null termination for keyString()
	return out + 1;
}

/**
 * @brief This function replaces escaped characters in a key name with unescaped characters.
 *
 * @pre The variable `mapping->buffer` needs to be as large as the size of the name of `key`.
 *
 * @param key This `Key` stores a name possibly containing escaped special characters.
 * @param mapping This variable stores the buffer and the character mapping this function uses to decode the name of `key`.
 *
 * @return A copy of `key` containing an unescaped version of the name of `key`
 */
CppKey decodeName (CppKey const & key, CCodeData * const mapping)
{
	CppKey unescaped{ key.dup () };
	unescaped.setName (key.getNamespace ());
	auto keyIterator = key.begin ();

	while (++keyIterator != key.end ())
	{
		string part = *keyIterator;
		size_t length = decode (part.c_str (), part.length () + 1, mapping);
		string decoded = string (reinterpret_cast<char *> (mapping->buffer), length);
		unescaped.addBaseName (decoded);
	}
	ELEKTRA_LOG_DEBUG ("Decoded name of “%s” is “%s”", key.getName ().c_str (), unescaped.getName ().c_str ());
	return unescaped;
}

/**
 * @brief This function replaces unescaped characters in a key name with escaped characters.
 *
 * @pre The variable `mapping->buffer` needs to be twice as large as the size of the name of `key`.
 *
 * @param key This `Key` stores a name possibly containing unescaped special characters.
 * @param mapping This variable stores the buffer and the character mapping this function uses to encode the name of `key`.
 *
 * @return A copy of `key` containing an escaped version of the name of `key`
 */
CppKey encodeName (CppKey const & key, CCodeData * const mapping)
{
	CppKey escaped{ key.dup () };
	escaped.setName (key.getNamespace ());
	auto keyIterator = key.begin ();

	while (++keyIterator != key.end ())
	{
		string part = *keyIterator;
		size_t length = encode (part.c_str (), part.length () + 1, mapping);
		string encoded = string (reinterpret_cast<char *> (mapping->buffer), length);
		escaped.addBaseName (encoded);
	}
	ELEKTRA_LOG_DEBUG ("Encoded name of “%s” is “%s”", key.getName ().c_str (), escaped.getName ().c_str ());
	return escaped;
}

} // end namespace

extern "C" {

/**
 * @brief This function replaces escaped characters in a key value with unescaped characters.
 *
 * The function stores the unescaped result value both in `mapping->buffer` and the given key.
 *
 * @pre The variable `mapping->buffer` needs to be as large as the key value’s size.
 *
 * @param key This key holds the value this function decodes.
 * @param mapping This variable stores the buffer and the character mapping this function uses to decode the value of the given key.
 */
void decodeKey (Key * key, CCodeData * mapping)
{
	const char * value = static_cast<const char *> (keyValue (key));
	if (!value) return;

	keySetRaw (key, mapping->buffer, decode (value, keyGetValueSize (key), mapping));
}

/**
 * @brief This function replaces unescaped characters in a key value with escaped characters.
 *
 * The function stores the escaped result value both in `mapping->buffer` and the given key.
 *
 * @pre The variable `mapping->buffer` needs to be twice as large as the key value’s size.
 *
 * @param key This key stores the value this function escapes.
 * @param mapping This variable stores the buffer and the character mapping this function uses to encode the value of the given key.
 */
void encodeKey (Key * key, CCodeData * mapping)
{
	const char * value = static_cast<const char *> (keyValue (key));
	if (!value) return;

	keySetRaw (key, mapping->buffer, encode (value, keyGetValueSize (key), mapping));
}

// ====================
// = Plugin Interface =
// ====================

/** @see elektraDocOpen */
int elektraCcodeOpen (Plugin * handle, Key * key ELEKTRA_UNUSED)
{
	CCodeData * const mapping = new CCodeData ();

	/* Store for later use...*/
	elektraPluginSetData (handle, mapping);

	KeySet * const config = elektraPluginGetConfig (handle);

	Key const * const escape = ksLookupByName (config, "/escape", 0);
	mapping->escape = '\\';
	if (escape && keyGetBaseNameSize (escape) && keyGetValueSize (escape) == 3)
	{
		int escapeChar = elektraHexcodeConvFromHex (keyString (escape)[1]);
		escapeChar += elektraHexcodeConvFromHex (keyString (escape)[0]) * 16;

		mapping->escape = escapeChar & 255;
	}
	ELEKTRA_LOG_DEBUG ("Use “%c” as escape character", mapping->escape);

	Key const * const root = ksLookupByName (config, "/chars", 0);

	if (root)
	{
		readConfig (mapping, config, root);
	}
	else
	{
		setDefaultConfig (mapping);
	}

	return ELEKTRA_PLUGIN_STATUS_NO_UPDATE;
}

/** @see elektraDocClose */
int elektraCcodeClose (Plugin * handle, Key * key ELEKTRA_UNUSED)
{
	CCodeData * const mapping = static_cast<CCodeData *> (elektraPluginGetData (handle));

	delete[](mapping->buffer);
	delete (mapping);

	return ELEKTRA_PLUGIN_STATUS_NO_UPDATE;
}

/** @see elektraDocGet */
int elektraCcodeGet (Plugin * handle, KeySet * returned, Key * parentKey)
{
	if (!strcmp (keyName (parentKey), "system/elektra/modules/ccode"))
	{
		KeySet * const pluginConfig = contract ();
		ksAppend (returned, pluginConfig);
		ksDel (pluginConfig);
		return ELEKTRA_PLUGIN_STATUS_SUCCESS;
	}

	CCodeData * const mapping = retrieveMapping (handle);

	CppKeySet keys{ returned };
	CppKeySet unescaped{};
	for (auto key : keys)
	{
		size_t const size = max (key.getBinarySize (), key.getNameSize ());
		if (size > mapping->bufferSize)
		{
			mapping->bufferSize = size;
			mapping->buffer = new unsigned char[mapping->bufferSize];
		}

		CppKey decoded = decodeName (key, mapping);
		decodeKey (*decoded, mapping);
		unescaped.append (decoded);
	}

	keys.release ();
	ksCopy (returned, unescaped.getKeySet ());
	ksDel (unescaped.release ());
	return ELEKTRA_PLUGIN_STATUS_SUCCESS;
}

/** @see elektraDocSet */
int elektraCcodeSet (Plugin * handle, KeySet * returned, Key * parentKey ELEKTRA_UNUSED)
{
	CCodeData * const mapping = retrieveMapping (handle);

	CppKeySet keys{ returned };
	CppKeySet escaped{};
	for (auto key : keys)
	{
		size_t const size = max (key.getBinarySize (), key.getNameSize ()) * 2;
		if (size > mapping->bufferSize)
		{
			mapping->bufferSize = size;
			mapping->buffer = new unsigned char[mapping->bufferSize];
		}

		CppKey encoded = encodeName (key, mapping);
		encodeKey (*encoded, mapping);
		escaped.append (encoded);
	}

	keys.release ();
	ksCopy (returned, escaped.getKeySet ());
	ksDel (escaped.release ());
	return ELEKTRA_PLUGIN_STATUS_SUCCESS;
}

Plugin * ELEKTRA_PLUGIN_EXPORT (ccode)
{
	// clang-format off
	return elektraPluginExport("ccode",
		ELEKTRA_PLUGIN_OPEN,	&elektraCcodeOpen,
		ELEKTRA_PLUGIN_CLOSE,	&elektraCcodeClose,
		ELEKTRA_PLUGIN_GET,	&elektraCcodeGet,
		ELEKTRA_PLUGIN_SET,	&elektraCcodeSet,
		ELEKTRA_PLUGIN_END);
}

} // end extern "C"
