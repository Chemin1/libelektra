/**
 * @file
 *
 * @brief Source for mmapstorage plugin
 *
 * @copyright BSD License (see doc/LICENSE.md or https://www.libelektra.org)
 *
 */



#include "mmapstorage.h"

#include <kdbhelper.h>
#include <kdberrors.h>
#include <kdbprivate.h>
#include <kdblogger.h>

//#include <fcntl.h>	// open()
#include <errno.h>
#include <stdio.h>	// fopen()
#include <unistd.h>	// close(), ftruncate()
#include <sys/mman.h>	// mmap()
#include <sys/stat.h>	// stat()
#include <sys/types.h>	// ftruncate ()
#include <assert.h>


#define SIZEOF_KEY		(sizeof (Key))
#define SIZEOF_KEY_PTR		(sizeof (Key *))
#define SIZEOF_KEYSET		(sizeof (KeySet))
#define SIZEOF_KEYSET_PTR	(sizeof (KeySet *))
#define SIZEOF_MMAPHEADER	(sizeof (MmapHeader))


static FILE * elektraMmapstorageOpenFile (Key * parentKey, int errnosave)
{
	FILE * fp;
	ELEKTRA_LOG ("opening file %s", keyString (parentKey));

	if ((fp = fopen (keyString (parentKey), "r+")) == 0) {
		ELEKTRA_SET_ERROR_GET (parentKey);
		ELEKTRA_LOG_WARNING ("error opening file %s", keyString (parentKey));
		ELEKTRA_LOG_WARNING ("strerror: %s", strerror (errno));
		errno = errnosave;
	}
	return fp;
}

static int elektraMmapstorageTruncateFile (FILE * fp, size_t mmapsize, Key * parentKey, int errnosave)
{
	ELEKTRA_LOG ("truncating file %s", keyString (parentKey));

	// TODO: does it matter whether we use truncate or ftruncate?
	int fd = fileno (fp);
	if ((ftruncate (fd, mmapsize)) == -1) {
		ELEKTRA_SET_ERROR_GET (parentKey);
		ELEKTRA_LOG_WARNING ("error truncating file %s", keyString (parentKey));
		ELEKTRA_LOG_WARNING ("mmapsize: %zu", mmapsize);
		ELEKTRA_LOG_WARNING ("strerror: %s", strerror (errno));
		errno = errnosave;
		return -1;
	}
	return 1;
}

static int elektraMmapstorageStat (struct stat * sbuf, Key * parentKey, int errnosave)
{
	ELEKTRA_LOG ("stat() on file %s", keyString (parentKey));

	if (stat(keyString (parentKey), sbuf) == -1) {
		ELEKTRA_SET_ERROR_GET (parentKey);
		ELEKTRA_LOG_WARNING ("error on stat() for file %s", keyString (parentKey));
		ELEKTRA_LOG_WARNING ("strerror: %s", strerror (errno));
		errno = errnosave;
		return -1;
	}
	return 1;
}

static char * elektraMmapstorageMapFile (void * addr, FILE * fp, size_t mmapSize, int mapOpts , Key * parentKey, int errnosave)
{
	ELEKTRA_LOG ("mapping file %s", keyString (parentKey));

	int fd = fileno (fp);
	char * mappedRegion = mmap (addr, mmapSize, PROT_READ | PROT_WRITE, mapOpts, fd, 0);
	if (mappedRegion == MAP_FAILED) {
		ELEKTRA_SET_ERROR_GET (parentKey);
		ELEKTRA_LOG_WARNING ("error mapping file %s", keyString (parentKey));
		ELEKTRA_LOG_WARNING ("mmapSize: %zu", mmapSize);
		ELEKTRA_LOG_WARNING ("strerror: %s", strerror (errno));
		errno = errnosave;
		return MAP_FAILED;
	}
	return mappedRegion;
}

#ifdef DEBUG
int findOrInsert (Key * key, DynArray * dynArray)
#else
static int findOrInsert (Key * key, DynArray * dynArray)
#endif
{
	size_t l = 0;
	size_t h = dynArray->size;
	size_t m;
	ELEKTRA_LOG_WARNING ("l: %zu", l);
	ELEKTRA_LOG_WARNING ("h: %zu", h);
	ELEKTRA_LOG_WARNING ("dynArray->size: %zu", dynArray->size);
	ELEKTRA_LOG_WARNING ("dynArray->alloc: %zu", dynArray->alloc);
	
	while (l < h)
	{
		m = (l+h)>>1;
		ELEKTRA_LOG_WARNING ("m: %zu", m);
		
		if (dynArray->keyArray[m] > key)
			h = m;
		else if (dynArray->keyArray[m] < key)
			l = ++m;
		else
			return 1; // found
	}
	// insert key at index l
	if (dynArray->size == dynArray->alloc)
	{
		// doubling the array size to keep reallocations logarithmic
		size_t oldAllocSize = dynArray->alloc;
		Key ** new = calloc (2 * oldAllocSize, sizeof (Key *));
		memcpy (new, dynArray->keyArray, dynArray->size * sizeof (Key *));
		free (dynArray->keyArray);
		dynArray->keyArray = new;
		dynArray->alloc = 2 * oldAllocSize;
	}

	memmove ((void *) (dynArray->keyArray+l+1), (void *) (dynArray->keyArray+l), ((dynArray->size)-l) * (sizeof (size_t)));
	dynArray->keyArray[l] = key;
	dynArray->size += 1;
	
	return 0;
}

static size_t find (Key * key, DynArray * dynArray)
{
	size_t l = 0;
	size_t h = dynArray->size;
	size_t m;
	ELEKTRA_LOG_WARNING ("l: %zu", l);
	ELEKTRA_LOG_WARNING ("h: %zu", h);
	ELEKTRA_LOG_WARNING ("dynArray->size: %zu", dynArray->size);
	ELEKTRA_LOG_WARNING ("dynArray->alloc: %zu", dynArray->alloc);
	
	while (l < h)
	{
		m = (l+h)>>1;
		ELEKTRA_LOG_WARNING ("m: %zu", m);
		
		if (dynArray->keyArray[m] > key)
			h = m;
		else if (dynArray->keyArray[m] < key)
			l = ++m;
		else
			return m; // found
	}
	
	assert (0);
}


static MmapHeader elektraMmapstorageDataSize (KeySet * returned, DynArray * dynArray)
{
	MmapHeader ret;
	
	Key * cur;
	ksRewind(returned);
	size_t dataBlocksSize = 0; // keyName and keyValue
	size_t metaKeySets = 0;
	size_t metaKeysWithDup = 0; // number of meta keys including copies
	while ((cur = ksNext (returned)) != 0)
	{
		dataBlocksSize += (cur->keySize + cur->keyUSize + cur->dataSize);
		
		if (cur->meta)
		{
			++metaKeySets;
			
			Key * curMeta;
			ksRewind(cur->meta);
			while ((curMeta = ksNext (cur->meta)) != 0)
			{
				if (findOrInsert (curMeta, dynArray) != 1)
				{
					// key was just inserted
					dataBlocksSize += (curMeta->keySize + curMeta->keyUSize + curMeta->dataSize);
				}
			}
			metaKeysWithDup += cur->meta->size; // still needed to store all the pointers
		}
			
	}

	size_t keyArraySize = (returned->size) * SIZEOF_KEY;
	size_t keyPtrArraySize = (returned->size) * SIZEOF_KEY_PTR;

	size_t mmapSize = SIZEOF_MMAPHEADER + SIZEOF_KEYSET + keyPtrArraySize + keyArraySize + dataBlocksSize \
 		+ (metaKeySets * SIZEOF_KEYSET) + (metaKeysWithDup * SIZEOF_KEY_PTR) + (dynArray->size * SIZEOF_KEY);

	ret.mmapSize = mmapSize;
	ret.dataSize = dataBlocksSize;
	ret.numKeys = returned->size;
	ret.numMetaKeySets = metaKeySets;
	ret.numMetaKeys = dynArray->size;
	return ret;
}

static void writeKeySet (MmapHeader mmapHeader, KeySet * keySet, KeySet * dest, DynArray * dynArray)
{
	KeySet * ksPtr = dest;
	char * ksArrayPtr = (((char *) ksPtr) + SIZEOF_KEYSET);
	char * keyPtr = (ksArrayPtr + (keySet->size * SIZEOF_KEY_PTR));
	char * dataPtr = (keyPtr + (keySet->size * SIZEOF_KEY));
	char * metaPtr = (dataPtr + (mmapHeader.dataSize));
	char * metaKsPtr = (metaPtr + (dynArray->size * sizeof (Key)));
	
	Key ** curKsArrayPtr = (Key **) ksArrayPtr;
	
	// allocate space in DynArray to remember the addresses of meta keys
	dynArray->mappedKeyArray = calloc (dynArray->size, sizeof (Key *));

	// first write the meta keys into place
	ELEKTRA_LOG_WARNING ("writing META keys");
	Key * mmapMetaKey;
	Key * curMeta;
	void * metaKeyNamePtr;
	void * metaKeyValuePtr;
	for (size_t i = 0; i < dynArray->size; ++i)
	{
		ELEKTRA_LOG_WARNING ("index: %zu", i);
		curMeta = dynArray->keyArray[i]; // old key location
		mmapMetaKey = (Key *) (metaPtr + (i * SIZEOF_KEY)); // new key location
		
		ELEKTRA_LOG_WARNING ("meta mmap location ptr: %p", (void *) ((Key *) metaPtr + (i * SIZEOF_KEY)));
		ELEKTRA_LOG_WARNING ("meta old location ptr: %p", (void *) curMeta);
		ELEKTRA_LOG_WARNING ("%p key: %s, string: %s", (void *)curMeta, keyName (curMeta), keyString (curMeta));
		
		size_t keyNameSize = curMeta->keySize + curMeta->keyUSize;
		size_t keyValueSize = curMeta->dataSize;
		
		// move Key name
		memcpy (dataPtr, curMeta->key, keyNameSize);
		metaKeyNamePtr = dataPtr;
		dataPtr += keyNameSize;

		// move Key value
		memcpy (dataPtr, curMeta->data.v, keyValueSize);
		metaKeyValuePtr = dataPtr;
		dataPtr += keyValueSize;
		
		// move Key itself
		memcpy (mmapMetaKey, curMeta, SIZEOF_KEY);
		mmapMetaKey->flags |= KEY_FLAG_MMAP;
		mmapMetaKey->key = metaKeyNamePtr;
		mmapMetaKey->data.v = metaKeyValuePtr;
		
		dynArray->mappedKeyArray[i] = mmapMetaKey;
	}
	
	Key * cur;
	Key * mmapKey;
	void * keyNamePtr;
	void * keyValuePtr;
	size_t keyIndex = 0;
	ksRewind(keySet);
	while ((cur = ksNext (keySet)) != 0)
	{
		mmapKey = (Key *) (keyPtr + (keyIndex * SIZEOF_KEY));
		size_t keyNameSize = cur->keySize + cur->keyUSize;
		size_t keyValueSize = cur->dataSize;

		// move Key name
		memcpy (dataPtr, cur->key, keyNameSize);
		keyNamePtr = dataPtr;
		dataPtr += keyNameSize;

		// move Key value
		memcpy (dataPtr, cur->data.v, keyValueSize);
		keyValuePtr = dataPtr;
		dataPtr += keyValueSize;
		
		// write the meta KeySet
		KeySet * oldMeta = cur->meta;
		KeySet * newMeta = 0;
		if (cur->meta)
		{
			newMeta = (KeySet *) metaKsPtr;
			memcpy (newMeta, oldMeta, sizeof (KeySet));
			metaKsPtr += sizeof (KeySet);
			
			newMeta->flags |= KS_FLAG_MMAP;
			newMeta->array = (Key **) (metaKsPtr);
			
			ksRewind(oldMeta);
			size_t metaKeyIndex = 0;
			Key * mappedMetaKey = 0;
			while ((curMeta = ksNext (oldMeta)) != 0)
			{
				// get address of mapped key and store it in the new array
				mappedMetaKey = dynArray->mappedKeyArray[find (curMeta, dynArray)];
				newMeta->array[++metaKeyIndex] = mappedMetaKey;
			}
			metaKsPtr += newMeta->size * sizeof (Key *);
		}
		
		
		// move Key itself
		memcpy (mmapKey, cur, SIZEOF_KEY);
		mmapKey->flags |= KEY_FLAG_MMAP;
		mmapKey->key = keyNamePtr;
		mmapKey->data.v = keyValuePtr;
		mmapKey->meta = newMeta;
		
		// write the Key pointer into the KeySet array
		memcpy (++curKsArrayPtr, mmapKey, SIZEOF_KEY);
		
		
		++keyIndex;
	}
	
	memcpy (ksPtr, keySet, SIZEOF_KEYSET);
	ksPtr->flags |= KS_FLAG_MMAP;
	ksPtr->array = (Key **) ksArrayPtr;
}

static void elektraMmapstorageWrite (char * mappedRegion, KeySet * keySet, MmapHeader mmapHeader, DynArray * dynArray)
{
	// multiple options for writing the KeySet:
	//		* fwrite () directly from the structs (needs multiple fwrite () calls)
	//		* memcpy () to continuous region and then fwrite () only once
	//		* use mmap to write to temp file and msync () after all data is written, then rename file
	mmapHeader.addr = mappedRegion;
	memcpy (mappedRegion, &mmapHeader, SIZEOF_MMAPHEADER);
	
	KeySet * ksPtr = (KeySet *) (mappedRegion + SIZEOF_MMAPHEADER);

	if (keySet->size < 1)
	{
		// TODO: review mpranj
		memcpy (ksPtr, keySet, SIZEOF_KEYSET);
		ksPtr->flags |= KS_FLAG_MMAP;
		return;
	}
	
	writeKeySet (mmapHeader, keySet, ksPtr, dynArray);
}

static void mmapToKeySet (char * mappedRegion, KeySet * returned)
{
	KeySet * keySet = (KeySet *) (mappedRegion + SIZEOF_MMAPHEADER);
	returned->array = keySet->array;
	returned->size 	= keySet->size;
	returned->alloc = keySet->alloc;
	ksRewind(returned); // cursor = 0; current = 0
	returned->flags = keySet->flags;
}

static void * mmapAddr (FILE * fp)
{
	char buf[SIZEOF_MMAPHEADER];
	memset (buf, 0, SIZEOF_MMAPHEADER * (sizeof (char)));
	
	fread (buf, SIZEOF_MMAPHEADER, (sizeof (char)), fp);
	MmapHeader * mmapHeader = (MmapHeader *) buf;
	
	if (mmapHeader->mmapMagicNumber == ELEKTRA_MAGIC_MMAP_NUMBER)
		return mmapHeader->addr;
	
	return 0;
}

int elektraMmapstorageOpen (Plugin * handle ELEKTRA_UNUSED, Key * errorKey ELEKTRA_UNUSED)
{
	// plugin initialization logic
	// this function is optional

	return ELEKTRA_PLUGIN_STATUS_SUCCESS;
}

int elektraMmapstorageClose (Plugin * handle ELEKTRA_UNUSED, Key * errorKey ELEKTRA_UNUSED)
{
	// free all plugin resources and shut it down
	// this function is optional

	// munmap (mappedRegion, sbuf.st_size);
	// close (fd);

	return ELEKTRA_PLUGIN_STATUS_SUCCESS;
}

int elektraMmapstorageGet (Plugin * handle ELEKTRA_UNUSED, KeySet * returned, Key * parentKey)
{
	if (!elektraStrCmp (keyName (parentKey), "system/elektra/modules/mmapstorage"))
	{
		KeySet * contract =
			ksNew (30, keyNew ("system/elektra/modules/mmapstorage", KEY_VALUE, "mmapstorage plugin waits for your orders", KEY_END),
			       keyNew ("system/elektra/modules/mmapstorage/exports", KEY_END),
			       keyNew ("system/elektra/modules/mmapstorage/exports/open", KEY_FUNC, elektraMmapstorageOpen, KEY_END),
			       keyNew ("system/elektra/modules/mmapstorage/exports/close", KEY_FUNC, elektraMmapstorageClose, KEY_END),
			       keyNew ("system/elektra/modules/mmapstorage/exports/get", KEY_FUNC, elektraMmapstorageGet, KEY_END),
			       keyNew ("system/elektra/modules/mmapstorage/exports/set", KEY_FUNC, elektraMmapstorageSet, KEY_END),
			       keyNew ("system/elektra/modules/mmapstorage/exports/error", KEY_FUNC, elektraMmapstorageError, KEY_END),
			       keyNew ("system/elektra/modules/mmapstorage/exports/checkconf", KEY_FUNC, elektraMmapstorageCheckConfig, KEY_END),
#include ELEKTRA_README (mmapstorage)
			       keyNew ("system/elektra/modules/mmapstorage/infos/version", KEY_VALUE, PLUGINVERSION, KEY_END), KS_END);
		ksAppend (returned, contract);
		ksDel (contract);

		return ELEKTRA_PLUGIN_STATUS_SUCCESS;
	}
	// get all keys

	int errnosave = errno;
	FILE * fp;

	if ((fp = elektraMmapstorageOpenFile (parentKey, errnosave)) == 0)
	{
		return -1;
	}

	struct stat sbuf;
	if (elektraMmapstorageStat (&sbuf, parentKey, errnosave) != 1)
	{
		fclose (fp);
		return -1;
	}
	
	void * addr = mmapAddr (fp);
	char * mappedRegion = MAP_FAILED;
	
	// can not use MAP_FIXED with addr = 0
	if (!addr)
		mappedRegion = elektraMmapstorageMapFile (addr, fp, sbuf.st_size, MAP_PRIVATE, parentKey, errnosave);
	else
		mappedRegion = elektraMmapstorageMapFile (addr, fp, sbuf.st_size, MAP_PRIVATE | MAP_FIXED, parentKey, errnosave);
	
	if (mappedRegion == MAP_FAILED)
	{
		fclose (fp);
		ELEKTRA_LOG ("mappedRegion == MAP_FAILED");
		return -1;
	}
	ELEKTRA_LOG_WARNING ("mappedRegion size: %zu", sbuf.st_size);
	ELEKTRA_LOG_WARNING ("mappedRegion ptr: %p", (void *) mappedRegion);

	ksClose (returned);
	mmapToKeySet (mappedRegion, returned);

	fclose (fp);
	return ELEKTRA_PLUGIN_STATUS_SUCCESS;
}

int elektraMmapstorageSet (Plugin * handle ELEKTRA_UNUSED, KeySet * returned, Key * parentKey)
{
	// set all keys

	int errnosave = errno;
	FILE * fp;

	if ((fp = elektraMmapstorageOpenFile (parentKey, errnosave)) == 0)
	{
		return -1;
	}
	
	DynArray dynArray;
	dynArray.keyArray = calloc (1, sizeof (Key *));
	dynArray.mappedKeyArray = 0;
	dynArray.size = 0;
	dynArray.alloc = 1;

	// TODO: calculating mmap size not needed if using fwrite() instead of mmap to write to file
	MmapHeader mmapHeader = elektraMmapstorageDataSize (returned, &dynArray);
	mmapHeader.mmapMagicNumber = ELEKTRA_MAGIC_MMAP_NUMBER;
	ELEKTRA_LOG_WARNING ("elektraMmapstorageSet -------> mmapsize: %zu", mmapHeader.mmapSize);

	if (elektraMmapstorageTruncateFile (fp, mmapHeader.mmapSize, parentKey, errnosave) != 1)
	{
		fclose (fp);
		return -1;
	}

	char * mappedRegion = elektraMmapstorageMapFile ((void *) 0, fp, mmapHeader.mmapSize, MAP_SHARED, parentKey, errnosave);
	ELEKTRA_LOG_WARNING ("mappedRegion ptr: %p", (void *) mappedRegion);
	if (mappedRegion == MAP_FAILED)
	{
		fclose (fp);
		ELEKTRA_LOG ("mappedRegion == MAP_FAILED");
		return -1;
	}
	
	elektraMmapstorageWrite (mappedRegion, returned, mmapHeader, &dynArray);
	ksClose (returned);
	mmapToKeySet (mappedRegion, returned);
	
	if (dynArray.keyArray)
		elektraFree (dynArray.keyArray);
	if (dynArray.mappedKeyArray)
		elektraFree (dynArray.mappedKeyArray);
	fclose (fp);
	return ELEKTRA_PLUGIN_STATUS_SUCCESS;
}

int elektraMmapstorageError (Plugin * handle ELEKTRA_UNUSED, KeySet * returned ELEKTRA_UNUSED, Key * parentKey ELEKTRA_UNUSED)
{
	// handle errors (commit failed)
	// this function is optional

	return ELEKTRA_PLUGIN_STATUS_SUCCESS;
}

int elektraMmapstorageCheckConfig (Key * errorKey ELEKTRA_UNUSED, KeySet * conf ELEKTRA_UNUSED)
{
	// validate plugin configuration
	// this function is optional

	return ELEKTRA_PLUGIN_STATUS_NO_UPDATE;
}

Plugin * ELEKTRA_PLUGIN_EXPORT (mmapstorage)
{
	// clang-format off
	return elektraPluginExport ("mmapstorage",
		ELEKTRA_PLUGIN_OPEN,	&elektraMmapstorageOpen,
		ELEKTRA_PLUGIN_CLOSE,	&elektraMmapstorageClose,
		ELEKTRA_PLUGIN_GET,	&elektraMmapstorageGet,
		ELEKTRA_PLUGIN_SET,	&elektraMmapstorageSet,
		ELEKTRA_PLUGIN_ERROR,	&elektraMmapstorageError,
		ELEKTRA_PLUGIN_END);
}
