# WLM2PST test fixtures

Everything under `tests/fixtures/` is **synthetic**. No fixture in this tree
was derived from, or contains, real client or personal email. Senders and
recipients use the reserved `example.invalid` domain
(https://www.rfc-editor.org/rfc/rfc2606), subjects and bodies are invented
placeholder text, and any Hebrew content is ordinary public-domain phrasing
("שלום עולם" - "hello world") typed for test purposes only. Never add real
mail, real addresses, or real attachment content to this directory.

## `tests/fixtures/eml/`

One `.eml` file per case required by spec section 28. Unless a file is
deliberately malformed (see below), fixtures are RFC-5322/RFC-2045-correct:
CRLF line endings, valid `Date:` headers, and `Message-ID:` values of the
form `<fixture-NNN@example.invalid>`.

| File | Purpose |
|---|---|
| `utf8_plain.eml` | Plain-text body, `charset=utf-8`. |
| `utf8_html.eml` | HTML body, `charset=utf-8`. |
| `utf8_bom.eml` | UTF-8 byte-order-mark bytes (`EF BB BF`) at the very start of the file, before the headers. |
| `windows1255_hebrew.eml` | `charset=windows-1255`; body bytes are real cp1255-encoded Hebrew ("שלום עולם"), generated with `iconv`/Python `str.encode('cp1255')`, not re-typed by hand. |
| `iso8859_8_hebrew.eml` | Same Hebrew text, encoded `iso-8859-8`. |
| `quoted_printable.eml` | `Content-Transfer-Encoding: quoted-printable`, including a soft line break and an encoded byte (`=C3=A9`). |
| `base64_body.eml` | `Content-Transfer-Encoding: base64`, 76-column wrapped. |
| `rfc2047_subject.eml` | `Subject:` is an RFC 2047 `=?UTF-8?B?...?=` encoded word carrying Hebrew text. |
| `hebrew_attachment_name.eml` | `multipart/mixed` with one attachment whose `Content-Disposition` carries both an RFC 2231 `filename*=UTF-8''%D7%...` parameter and an RFC 2047 `filename="=?UTF-8?B?...?="` parameter, both encoding the same Hebrew filename. |
| `multipart_alternative.eml` | `multipart/alternative` with a plain-text part and an HTML part. |
| `inline_cid_image.eml` | `multipart/related` HTML body referencing `cid:tinyimage001@example.invalid`, plus a tiny synthetic 1x1 PNG part with matching `Content-ID`. |
| `single_attachment.eml` | `multipart/mixed` with exactly one small synthetic text attachment. |
| `multiple_attachments.eml` | `multipart/mixed` with two small synthetic attachments. |
| `nested_eml_attachment.eml` | `multipart/mixed` with one `message/rfc822` part containing a complete, independently-valid inner EML. |
| `empty_body.eml` | Valid headers, separator present, zero-length body. |
| `missing_subject.eml` | No `Subject:` header. |
| `missing_date.eml` | No `Date:` header. |
| `invalid_date.eml` | `Date: Sun, 30 Feb 2020 10:00:00 +0000` - structurally parseable but calendar-invalid (February has no 30th), so `parse_rfc5322_date` must return `nullopt`. |
| `multiple_received.eml` | Exactly three `Received:` headers, in file order. |
| `x_unsent.eml` | `X-Unsent: 1` (Windows Live Mail draft marker). |
| `malformed_line_endings.eml` | Header section mixes CRLF, bare LF, and bare CR line terminators; still resolves to a normal CRLF separator and body. |
| `header_nul.eml` | A literal `NUL` (`0x00`) byte embedded inside a header value (`Subject:`). |
| `missing_separator.eml` | Headers with no blank-line header/body separator at all; the first non-header-shaped line (no colon) is where the body begins. |
| `broken_mime_boundary.eml` | Declares a MIME boundary that is truncated mid-part; the closing `--boundary--` delimiter never appears. |
| `zero_byte.eml` | Exactly 0 bytes. |

A very large synthetic attachment fixture is intentionally **not** committed;
per spec section 28 it is generated on the fly during test execution instead.

## Generation notes

Fixtures with raw non-ASCII/binary bytes (BOM, cp1255/iso-8859-8 Hebrew, the
literal NUL byte, the tiny PNG) were produced with a short one-off Python
script (using `str.encode(...)` for the charset conversions) rather than
typed by hand, so the on-disk bytes are exactly what the target encoding
produces. The script itself is not part of the repository; only its output
(the fixture files) is committed.
