/**
 * @file
 *
 * @brief Read key sets using yaml-cpp
 *
 * @copyright BSD License (see LICENSE.md or https://www.libelektra.org)
 */

#include "read.hpp"
#include "yaml.h"

#include <kdb.hpp>
#include <kdblogger.h>
#include <kdbplugin.h>

#include <sstream>

using namespace std;
using namespace kdb;

namespace
{
/**
* @brief Convert a YAML node to a key set
*
* @param node This YAML node stores the data that should be added to the keyset `mappings`
* @param mappings The key set where the YAML data will be stored
* @param prefix This string stores a prefix for the key name
*/
void convertNodeToKeySet (YAML::Node const & node, KeySet & mappings, string const & prefix)
{
	for (auto element : node)
	{
		Key key (prefix, KEY_END);
		key.addBaseName (element.first.as<string> ());
		key.set<string> (element.second.as<string> ());
		ELEKTRA_LOG_DEBUG ("%s: %s", key.get<string> ().c_str (), key.getName ().c_str ());
		mappings.append (key);
	}
}
} // end namespace

/**
 * @brief Read a YAML file and add the resulting data to a given key set
 *
 * @param mappings The key set where the YAML data will be stored
 * @param parent This key stores the path to the YAML data file that should be read
 */
void yamlcpp::yamlRead (KeySet & mappings, Key const & parent)
{
	YAML::Node config = YAML::LoadFile (parent.getString ());
	ostringstream data;
	data << config;

	ELEKTRA_LOG_DEBUG ("Data: “%s”", data.str ().c_str ());
	convertNodeToKeySet (config, mappings, parent.getFullName ());
	ELEKTRA_LOG_DEBUG ("Number of keys: %zd", mappings.size ());
}
