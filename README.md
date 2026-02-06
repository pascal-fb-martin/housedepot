# HouseDepot

A House web service to maintain configurations, scripts and other resource files.

## Overview

This is a web server application that stores files for other services, so that services can access their configuration independently of the computer they currently run on.

That application also provides a central point through which all configuration files can be accessed and updated, facilitating the deployement of configurations when there is a fair number of computers involved (e.g. five or six Raspberry Pi computers).

On the client application side, the goal is to provide a simple Web API (GET, PUT and POST methods), with no extensions beside a few optional parameters, that implements a specified behavior based on URL matching (see below for a description of the web API).

The web API implements a very specific behavior:

- An history of all updates is maintained by versioning every file. Each file is versioned independently from each other.

- Any update to a revision controled file becomes the latest and current version, available to download through standard HTTP GET with no parameter required.

- A standard PUT request with no parameter writes the content of a new revision. (For a client not aware of the revision history, this overwrites the file and appears as idempotent, which is why the PUT method is used for this operation.)

- A web API allows administrator to change the current version or to apply user defined tags (tags are mostly intended for documentation purposes).

A client service does not need to be concerned about versioning: it may just download the current configuration using the default URL, as if HouseDepot was a standard web server providing static content.

Upload to HouseDepot is also a standard PUT request, compatible with most available web tools (e.g. wget or curl).

Note that HouseDepot is not intended to be used as a source configuration system: this is intended for a production system where one may need to revert a configuration change or to review when and what changes occurred.

There is no commit comment: instead one may assign tags to revisions (including multiple tags to the same revision).

It is the intent of the design to allow multipe HouseDepot services to run concurrently, for redundancy. The clients are responsible for sending updates to all active HouseDepot services: there is no synchronization between the HouseDepot services themselves.

Access to HouseDepot is not restricted: this service should only be accessible from a private network, and the data should not be sensitive. *Do not store cryptographic keys or other secrets using HouseDepot.*

## Installation

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install

## Files and Repositories

A revision controled file is simply a unique URL path that can be both retrieved and stored by the applications. HouseDepot silently records the history of every change.

A revision controled file can be any ASCII or UTF-8 text format.

An application is normally concerned only with doing GET of the current version and PUT of any subsequent update. It is the intent of the design that any file history management will be implemented as a web UI to HouseDepot. This way history management functions do not need to be duplicated across services.

Since a file may be modified by another instance of the same service, it is recommended that each application requests the same files periodically.

This web API supports multiple repositories, each with its own root URI. Some repositories are installed by default, more can be created by the user (just create the directory in the depot repositories root). The default repositories (and their default directory paths) are:

```
/depot/config (/var/lib/house/depot/config)
/depot/state (/var/lib/house/depot/state)
/depot/scripts (/var/lib/house/depot/scripts)
```

(Previous versions of HouseDepot used the default root /var/lib/house instead of /var/lib/house/depot. An upgrade will automatically move the defaults repositories to the new location. Any user created repository must be moved by hand.)

Files within each repository can be created in the repository's directory, or in a subdirectory matching a group or host name (recommended). The use of group names makes it possible to maintain multiple sets of configurations within the same HouseDepot service. The use of host name makes it possible to maintain hardware-specific configurations. (A group name is a name provided through the `-group` option. House client services will automatically look for files based on their current group or host--depending on the application.)

The user may define his own repository root with the -root option. This option is mostly intended for testing. For example:

```
housedepot -root=/home/smith/depot
```

The housedepot service launched in the example above will retrieve the repositories by scanning /home/smith/depot.

Per repository options can be specified by creating a `.options` file in thre repository top directory. This is an ASCII file where each line sets a specific option (name ' ' value). Only one option is supported at this time:
* depth (numeric, the maximum number of revisions kept by HouseDepot--there is no limit if the option is not present or the value  is 0)

No file or repository can be named "all". Character '~' is not allowed in file, repository or subdirectory names. Only alphabetical, numerical, '_' and '-' characters are allowed in tag names.

The path of each file relative to its root directory matches the path used in the HTTP URL. For example `/depot/config/cabin/sprinkler.json` matches file `/var/lib/house/depot/config/cabin/sprinkler.json`. However HouseDepot limits the depth of a repository to one subdirectory level only: attempts to create /depot/config/depot/cabin/woods/sprinkler.json would be rejected.

The user may restrict the list of reported files to only those matching a list of groups using the -authority option. This option takes a comma-separated list of groups. For example:

```
housedepot -authority=test,unittest
```

That option prevents the HouseDepot service from reporting files that are not within these groups. However the service still accept PUT requests for any file. This can be used to run multiple HouseDepot services without these competing with each other. Since each service accepts any checkin (PUT requests), they operate as backups of each other: if one server fails, just extend the list of groups in the `-authority` argument.

## Recommanded Practices

One of HouseDepot's goals is to facilitate moving services across a pool of computers and avoid leaving multiple (out of date) copies of their configuration lingering around.

A way to do this is to either have a single configuration file name across the network (for example if there is only one sprinkler controller active at any one time).

Sometimes multiple separate configurations are necessary. In that case, one can define a configuration name (a.k.a. group), which should be distinct from the set of computer names. When the service is moved from one computer to the other, the only local configuration needed is to choose the proper group name. (This is named a group because it is expected that a complete system configuration will include multiple services working in concert, i.e. a group of services. A local network might run multiple instances of such groups.)

However some services (e.g. HouseRelays or HouseSensor) depend on access to local hardware interfaces that are not present on other computers: in that case the recommended practice is to name the configuration based on the computer name.

In order to keep the purpose of each configuration file clear, it is recommended for the configuration path to retain the ID of the service it is related to. A configuration file name would then incorporate two parts: a group or computer name and a service name.

The allowance for one level of directory is intended to support this recommended organization: use the group or computer name as a subdirectory name, and keep the configuration file's base name matching the service ID.

## Web API

All responses from the HouseDepot server contain the following standard entries:

- .host: the name of the host that replied.
- .proxy: if present, the name of the HousePortal proxy to use.
- .timestamp: the time of this response.

```
GET /depot/check
```

Return a millisecond timestamp representing when the last change to any repository has occurred. This method allows for an efficient periodic polling of the service. The client should not use the returned timestamp for anything else than comparing it with a previous returned value. If the timestamp value has changed, regardless how, the client should query the repository content again.

The response is a JSON structure with the following entries:
- .updated: an integer representing the last repository update's timestamp.

```
GET /depot/all
```

Return the list of all repositories present on this HouseDepot service.

The response is a JSON structure with the following entries:

- .repositories: an array of strings. Each string is the root URI for one repository.

```
GET /depot/<path>/all
```

Return the list of all files present in the specified repository, or repository's subdirectory, with their current revision and date (i.e. the revision and file for the current version).

The response is a JSON structure with the following entries:

- .files: an array of JSON structure items. Each item represent one file with the following elements: .name, .rev and .time.

```
GET /depot/<name>/...
GET /depot/<name>/...?revision=<tag>
```

Retrieve the current revision of the specified file. If a `revision` parameter is present, the specified revision is retrieved. The tag may be a file revision number or a user-assigned name associated with one file revision.

Three tag names are reserved:

- `latest` represents the most recent revision, which might not be the same as current.
- `current` represents the current revision, i.e. `revision=current` behaves the same as if there was no revision parameter
- `all` causes the request to return an history of the file instead of its content.

A file history is a json object that contains the name of the server, a timestamp, the name of the proxy, an array of tag definitions and an array of revisions.

Each tag definitions is an array with 2 entries: the tag name and the revision number.

Each revision entry is an object with the following entries:

- .time: an ASCII representation of the revision (PUT) date and time
- .rev: the file's numeric revision number.

The arrays have no specified order. The historical order of revisions can be reconstitued either by sorting on date or revision number.

```
PUT /depot/<name>/...[?time=<timestamp>]
```

Store a new revision of the specified file. That revision becomes the current version of that file. Note that there is no revision's comment: this service is not intended to compete with Git..

The file may not exist, in which case it is created. The optional time parameter forces the file revision's timestamp to the specified value.

```
POST /depot/<name>/...?revision=<tag>
POST /depot/<name>/...?revision=<tag>&tag=<name>
POST /depot/<name>/...?tag=<name>
```

Assign a tag to the specified revision. If the `tag` parameter is not specified. this request assigns the `current` tag to the specified version. If the `tag` parameter is specified, this request assigns the specified tag to the specified revision and the `current` tag is not touched, unless the name of the tag is `current`. If the version is not specified, `current` is assumed.

The definition of tag is the same as for the GET method except that using name `all` is not allowed.

The file must exist.

```
DELETE /depot/<name>/...?revision=<tag>
```

Delete the specified tag or revision. If the revision parameter refers to a user defined tag, only the tag is removed. If the revision parameter refers to an actual revision number, this revision's file is deleted as well as all user-defined tags that refer to it.

It is not allowed to delete a predefined tag, or a revision that a predefined tag refers to.

It is planned to support revision=all, which would delete any occurrence of the file (all revisions and all tags) in one sweep. This is delayed until HouseDepot code base is considered stable.

## Configuration

There is no user configuration file.

## Debian Packaging

The provided Makefile supports building private Debian packages. These are _not_ official packages:

- They do not follow all Debian policies.

- They are not built using Debian standard conventions and tools.

- The packaging is not separate from the upstream sources, and there is
  no source package.

To build a Debian package, use the `debian-package` target:

```
make debian-package
```

