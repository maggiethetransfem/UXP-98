var Ci = Components.interfaces;
var Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");

var DISCORD_HOSTS = {
  "discord.com": true,
  "www.discord.com": true,
  "canary.discord.com": true,
  "ptb.discord.com": true
};

var POLYFILLS = "(" + function() {
  function define(obj, name, value) {
    try {
      if (!(name in obj)) {
        Object.defineProperty(obj, name, {
          configurable: true,
          writable: true,
          value: value
        });
      }
    } catch (e) {
      obj[name] = value;
    }
  }

  if (typeof Intl === "object") {
    if (typeof Intl.ListFormat !== "function") {
      var ListFormat = function(locales, options) {
        options = options || {};
        this._style = options.style || "long";
        this._type = options.type || "conjunction";
      };
      ListFormat.prototype.format = function(list) {
        list = Array.prototype.map.call(list || [], String);
        if (list.length === 0) {
          return "";
        }
        if (list.length === 1) {
          return list[0];
        }
        var sep = this._type === "disjunction" ? " or " : " and ";
        if (this._style === "narrow" || this._style === "short") {
          sep = this._type === "disjunction" ? " or " : ", ";
        }
        return list.slice(0, -1).join(", ") + sep + list[list.length - 1];
      };
      ListFormat.prototype.formatToParts = function(list) {
        var text = this.format(list);
        return text ? [{ type: "element", value: text }] : [];
      };
      ListFormat.supportedLocalesOf = function(locales) {
        return Array.isArray(locales) ? locales.slice() : (locales ? [locales] : []);
      };
      define(Intl, "ListFormat", ListFormat);
    }

    if (typeof Intl.DurationFormat !== "function") {
      var DurationFormat = function(locales, options) {
        this._style = options && options.style || "short";
      };
      DurationFormat.prototype.format = function(duration) {
        duration = duration || {};
        var units = [
          ["years", "y"], ["months", "mo"], ["weeks", "w"], ["days", "d"],
          ["hours", "h"], ["minutes", "m"], ["seconds", "s"],
          ["milliseconds", "ms"], ["microseconds", "us"], ["nanoseconds", "ns"]
        ];
        var out = [];
        for (var i = 0; i < units.length; i++) {
          var value = duration[units[i][0]];
          if (value) {
            out.push(String(value) + units[i][1]);
          }
        }
        return out.length ? out.join(" ") : "0s";
      };
      DurationFormat.prototype.formatToParts = function(duration) {
        return [{ type: "literal", value: this.format(duration) }];
      };
      DurationFormat.supportedLocalesOf = function(locales) {
        return Array.isArray(locales) ? locales.slice() : (locales ? [locales] : []);
      };
      define(Intl, "DurationFormat", DurationFormat);
    }

    if (typeof Intl.Segmenter !== "function") {
      var Segmenter = function(locales, options) {
        this._granularity = options && options.granularity || "grapheme";
      };
      Segmenter.prototype.segment = function(input) {
        var string = String(input);
        var segments = [];
        if (this._granularity === "word") {
          var re = /\S+|\s+/g;
          var match;
          while ((match = re.exec(string))) {
            segments.push({
              segment: match[0],
              index: match.index,
              input: string,
              isWordLike: /\S/.test(match[0])
            });
          }
        } else {
          for (var i = 0; i < string.length; i++) {
            segments.push({ segment: string.charAt(i), index: i, input: string });
          }
        }
        return {
          containing: function(index) {
            for (var i = 0; i < segments.length; i++) {
              var s = segments[i];
              if (index >= s.index && index < s.index + s.segment.length) {
                return s;
              }
            }
            return undefined;
          },
          get length() {
            return segments.length;
          },
          item: function(index) {
            return segments[index];
          },
          forEach: function(callback, thisArg) {
            segments.forEach(callback, thisArg);
          },
          map: function(callback, thisArg) {
            return segments.map(callback, thisArg);
          },
          slice: function() {
            return Array.prototype.slice.apply(segments, arguments);
          },
          toString: function() {
            return "[object Segments]";
          }
        };
      };
      Segmenter.supportedLocalesOf = function(locales) {
        return Array.isArray(locales) ? locales.slice() : (locales ? [locales] : []);
      };
      define(Intl, "Segmenter", Segmenter);
    }
  }

  if (window.crypto && typeof window.crypto.randomUUID !== "function") {
    define(window.crypto, "randomUUID", function() {
      var bytes = new Uint8Array(16);
      if (window.crypto.getRandomValues) {
        window.crypto.getRandomValues(bytes);
      } else {
        for (var i = 0; i < bytes.length; i++) {
          bytes[i] = Math.floor(Math.random() * 256);
        }
      }
      bytes[6] = (bytes[6] & 0x0f) | 0x40;
      bytes[8] = (bytes[8] & 0x3f) | 0x80;
      function hex(n) {
        return (n + 0x100).toString(16).slice(1);
      }
      return hex(bytes[0]) + hex(bytes[1]) + hex(bytes[2]) + hex(bytes[3]) + "-" +
             hex(bytes[4]) + hex(bytes[5]) + "-" +
             hex(bytes[6]) + hex(bytes[7]) + "-" +
             hex(bytes[8]) + hex(bytes[9]) + "-" +
             hex(bytes[10]) + hex(bytes[11]) + hex(bytes[12]) +
             hex(bytes[13]) + hex(bytes[14]) + hex(bytes[15]);
    });
  }

  if (typeof window.WebSocket === "function" &&
      !window.WebSocket.__discordCompatWrapped) {
    var NativeWebSocket = window.WebSocket;
    var WrappedWebSocket = function(url, protocols) {
      if (typeof url === "string" &&
          url.indexOf("wss://gateway.discord.gg/") === 0 &&
          url.indexOf("compress=zlib-stream") !== -1) {
        url = url.replace(/[?&]compress=zlib-stream(?=&|$)/, function(match) {
          return match.charAt(0) === "?" ? "?" : "";
        }).replace("?&", "?").replace(/&$/, "").replace(/\?$/, "");
      }
      if (arguments.length > 1) {
        return new NativeWebSocket(url, protocols);
      }
      return new NativeWebSocket(url);
    };
    WrappedWebSocket.prototype = NativeWebSocket.prototype;
    WrappedWebSocket.CONNECTING = NativeWebSocket.CONNECTING;
    WrappedWebSocket.OPEN = NativeWebSocket.OPEN;
    WrappedWebSocket.CLOSING = NativeWebSocket.CLOSING;
    WrappedWebSocket.CLOSED = NativeWebSocket.CLOSED;
    WrappedWebSocket.__discordCompatWrapped = true;
    try {
      Object.defineProperty(window, "WebSocket", {
        configurable: true,
        writable: true,
        value: WrappedWebSocket
      });
    } catch (e) {
      window.WebSocket = WrappedWebSocket;
    }
  }
} + ")();";

var observer = {
  observe: function(subject, topic, data) {
    if (topic !== "document-element-inserted") {
      return;
    }

    var doc = subject;
    var win = doc && doc.defaultView;
    var host = win && win.location && win.location.hostname;
    if (!host || !DISCORD_HOSTS[host]) {
      return;
    }

    try {
      var script = doc.createElementNS("http://www.w3.org/1999/xhtml", "script");
      script.textContent = POLYFILLS;
      (doc.documentElement || doc.head || doc).appendChild(script);
      if (script.parentNode) {
        script.parentNode.removeChild(script);
      }
    } catch (e) {
      Cu.reportError(e);
    }
  }
};

function install(data, reason) {}
function uninstall(data, reason) {}

function startup(data, reason) {
  Services.obs.addObserver(observer, "document-element-inserted", false);
}

function shutdown(data, reason) {
  Services.obs.removeObserver(observer, "document-element-inserted");
}
