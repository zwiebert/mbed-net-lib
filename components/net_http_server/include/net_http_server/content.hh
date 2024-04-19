/**
 * \file    net_http_server/content.hh
 * \brief   embedded files we want to serve.  classes to serve files.  (XXX)
 * \note    Mandatory files should be compressed by gzip, because its widely supported.  Brotli may be used for source maps.
 */
#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


/**
 * \brief base class for serving file like things
 *
 *        XXX: This used to have more than one subclass, but currently its back to just normal files. So its not really useful ATM.
 *
 */
class ContentReader {
public:
  virtual ~ContentReader() = default;
  virtual int open(const char *name, const char *query) = 0;
  virtual int read(int fd, char *buf, unsigned buf_size) = 0;
  virtual int close(int fd) = 0;
};

/**
 * \brief \ref ContentReader subclass to server files from file system
 */
class FileContentReader final: public ContentReader {
public:
  virtual int open(const char *name, const char *query = 0) {
    return ::open(name, O_RDONLY);
  }
  virtual int read(int fd, char *buf, unsigned buf_size) {
    return ::read(fd, buf, buf_size);
  }
  virtual int close(int fd) {
    return ::close(fd);
  }
private:
};

/**
 * \brief describe web content
 */
struct web_content {
  const char *content; ///<  content data as byte array
  const char *content_encoding;  ///< NULL or, if \ref content is compressed, the value for HTTP header CONTENT_ENCODING e.g. "gzip", "br"
  unsigned content_length; ///< byte-length of content data
};

/**
 * \brief  map URI to web content
 */
struct file_map {
  const char *uri;   ///<  URI  (e.g. "/index.html")
  const char *type;  ///< MIME type  (e.g. "text/javascript")
  struct web_content wc;  ///< content
  ContentReader *content_reader;  ///< if not NULL use this to provide the content data
};

/**
 * \brief          Look up file_map for given URI.
 *
 * \param uri      URI we want the file_map for
 * \return         pointer to file_map or NULL if none was defined for  URI
 */
const struct file_map* wc_getContent(const char *uri);

extern const web_content wapp_html_gz_fm;
extern const web_content wapp_js_gz_fm;
extern const web_content wapp_js_map_gz_fm;
extern const web_content wapp_css_gz_fm;
extern const web_content wapp_css_map_gz_fm;

extern const web_content wapp_html_br_fm;
extern const web_content wapp_js_br_fm;
extern const web_content wapp_js_map_br_fm;
extern const web_content wapp_css_br_fm;
extern const web_content wapp_css_map_br_fm;

