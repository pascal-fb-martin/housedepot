# HouseDepot
A House web service to maintain configurations, scripts and other resource files.
## Overview
This is a web server application that stores files for other services.

The goal is to provide a simple Web API (GET, PUT and POST methods), with no extensions beside a few optional parameters, that implements a specified behavior based on URL matching (see below for a description of the web API).

Its API implements a very specific behavior:

- An history of all updates is maintained by versioning every file. Each file is versioned independently from each other.

- Any update to a revision controled file becomes the latest and current version, available to download through HTTP.

- A web API allows changing the current version or applying user defined tags.

- A standard GET request with no parameter always return the current version.

The HouseDepot service is not meant to be accessed directly by end-users: this is a back office service that supports other services. Its web UI is intended for services administrators.

A client service does not need to be concerned about versioning: it may just download the current configuration using the default URL.

Note that HouseDepot is not intended to be used as a source configuration system: this is intended for a production system where one may need to revert a configuration change or to review when and what changes occurred.

There is no commit comment: instead one may assign tags to revisions (including multiple tags to the same revision).

It is the intent of the design to allow multipe HouseDepot services to run concurrently, for redundancy. The clients are responsible for sending updates to all active HouseDepot services: there is no synchronization between HouseDepot services.

Access to HouseDepot is not restricted: this service should only be accessible from a private network, and the data should not be sensitive. Do not store cryptographic keys using HouseDepot.

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

This web API supports multiple repositories, each with its own root URI. Some repositories are installed by default, others can be defined by the user. The default repositories (and their directory path) are:
```
/depot/config (/var/lib/house/config)
/depot/scripts (/var/lib/house/scripts)
```

The user can define his own repository /depot/*name* pointing to directory path *path* using the -repo=*name*:*path* command line argument. Multiple repositories can be defined by repeating the -repo option. For example:
```
housedepot -repo=userx:/var/lib/user/x -repo=userz:/var/lib/user/z
```
The housedepot service launched in the example above will accept revision controled files at /depot/userx and /depot/userz in addition to the default repositories.

The path of each file relative to its root directory matches the path used in the HTTP URL. For example `/depot/config/cabin/sprinkler.json` matches file `/var/lib/house/config/cabin/sprinkler.json`.

## Web API

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
- .date: an ASCII representation of the revision (POST) date and time
- .rev: the file's numeric revision number.

The arrays have no specified order. The historical order of revisions can be reconstitued either by sorting on date or revision number.

```
PUT /depot/<name>/...
```
Store a new revision of the specified file. That revision becomes the current version of that file. Note that there is no revision's comment: this service is not intended to compete with Git..

The file may not exist, in which case it is created.

```
POST /depot/<name>/...?revision=<tag>
POST /depot/<name>/...?revision=<tag>&tag=<name>
POST /depot/<name>/...?tag=<name>
```
Assign a tag to the specified revision. If the `tag` parameter is not specified. this request assigns the `current` tag to the specified version. If the `tag` parameter is specified, this request assigns the specified tag to the specified revision and the `current` tag is not touched, unless the name of the tag is `current`. If the version is not specified, `current` is assumed.

The definition of tag is the same as for the GET method except that using name `all` is not allowed.

The file must exist.

## Configuration

There is no user configuration file.

