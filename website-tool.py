#!/usr/bin/env python3

from urllib.request import url2pathname
import argparse
import gettext
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap


APP_NAME = 'dpScreenOCR'
APP_VERSION = '1.3.0'
APP_COPYRIGHT_YEAR = '2019-2023'
APP_AUTHOR = 'Daniel Plakhotich'
APP_AUTHOR_EMAIL = 'daniel.plakhotich@gmail.com'
BUGS_ADDRESS = 'https://github.com/danpla/dpscreenocr/issues'


SCRIPT_PATH = os.path.realpath(__file__)
SCRIPT_DIR = os.path.dirname(SCRIPT_PATH)
DATA_DIR = os.path.join(SCRIPT_DIR, 'website-tool-data')
PO_DIR = os.path.join(DATA_DIR, 'po')

# Mapping from POSIX-style locale codes to BCP 47 tags. The former is
# used for PO/MO file names, and the latter is for anything related to
# HTML. You should add a new entry to make a new translation, even if
# both codes are the same.
#
# See https://www.w3.org/International/geo/html-tech/tech-lang.html
LOCALE_TO_BCP47 = {
    'bg': 'bg',
    'ca': 'ca',
    'de': 'de',
    'es': 'es',
    'fr': 'fr',
    'hr': 'hr',
    'nb_NO': 'nb',
    'pa_PK': 'pa',
    'pl': 'pl',
    'ru': 'ru',
    'tr': 'tr',
    'uk': 'uk',
    'zh_CN': 'zh-Hans',
}


def N_(s):
    return s


def open_for_text_writing(path):
    # Note that we don't want platform-specific line endings since the
    # generated files will be in a VCS repository.
    return open(path, 'w', encoding='utf-8', newline='\n')


def get_exe_path(name):
    path = shutil.which(name)
    if not path:
        sys.exit('{} not found'.format(name))
    return path


def update_po(*, no_fuzzy_matching, keep_pot):
    xgettext_path = get_exe_path('xgettext')

    pot_path = os.path.join(PO_DIR, 'en.pot')

    subprocess.check_call((
        xgettext_path,
        '--from-code=UTF-8',
        '--no-location',
        '--add-comments=Translators:',
        '--package-name=' + APP_NAME + ' Website',
        '--msgid-bugs-address=' + BUGS_ADDRESS,
        '-kN_',
        '--output=' + pot_path,
        SCRIPT_PATH))

    # xgettext leaves the header's charset undefined if all source
    # strings are ASCII, even if --from-code is set to UTF-8. Fix it
    # manually.
    with open(pot_path, encoding='utf-8') as f:
        data = f.read()

    data = data.replace(
        '"Content-Type: text/plain; charset=CHARSET\\n"',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        1)

    with open_for_text_writing(pot_path) as f:
        f.write(data)

    msginit_path = get_exe_path('msginit')
    msgmerge_path = get_exe_path('msgmerge')

    for locale in LOCALE_TO_BCP47:
        po_path = os.path.join(PO_DIR, locale + '.po')

        if not os.path.exists(po_path):
            subprocess.check_call((
                msginit_path,
                '--locale=' + locale,
                '--no-translator',
                '--input=' + pot_path,
                '--output-file=' + po_path))
            continue

        msgmerge_args = [
            msgmerge_path,
            '--quiet',
            '--update',
            '--backup=off'
        ]

        if no_fuzzy_matching:
            msgmerge_args.append('--no-fuzzy-matching')

        msgmerge_args.extend((po_path, pot_path))

        subprocess.check_call(msgmerge_args)

    if not keep_pot:
        os.remove(pot_path)


def compile_po(out_dir):
    os.makedirs(out_dir, exist_ok=True)

    msgfmt_path = get_exe_path('msgfmt')

    missing_pos = []

    for locale in sorted(LOCALE_TO_BCP47):
        po_path = os.path.join(PO_DIR, locale + '.po')
        if not os.path.exists(po_path):
            missing_pos.append(locale + '.po')
            continue

        mo_path = os.path.join(out_dir, locale + '.mo')

        subprocess.check_call((
            msgfmt_path,
            '--output-file=' + mo_path,
            po_path))

    if missing_pos:
        print(
            'The following PO files don''t exists; run update_po to '
            'generate them: {}'.format(', '.join(missing_pos)))


# Returns a mapping from a BCP 47 tag to gettext.*Translations.
def collect_langs(mo_dir):
    langs = {}
    langs['en'] = gettext.NullTranslations()

    untranslated_langs = []

    for locale in sorted(LOCALE_TO_BCP47):
        mo_path = os.path.join(mo_dir, locale + '.mo')
        lang_code = LOCALE_TO_BCP47[locale]

        if not os.path.exists(mo_path):
            # OK, update_po wasn't called.
            continue

        with open(mo_path, 'rb') as f:
            translator = gettext.GNUTranslations(f)

        if (translator.gettext(GETTEXT_LANGUAGE_NAME_KEY)
                == GETTEXT_LANGUAGE_NAME_KEY):
            untranslated_langs.append(lang_code)
            continue

        langs[lang_code] = translator

    if untranslated_langs:
        print(
            'The following languages are skipped since their '
            '{} msgid is not translated: {}'.format(
                GETTEXT_LANGUAGE_NAME_KEY,
                ', '.join(untranslated_langs)))

    return langs


def indent(text):
    return textwrap.indent(text, '  ')


def get_url_to_root(url):
    assert not '//' in url
    return '/'.join(('..', ) * url.count('/'))


def srip_index_html(url):
    index_html = 'index.html'
    if url == index_html or url.endswith('/' + index_html):
        return url[:-len(index_html)]

    return url


def write_root_index_page(langs):
    with open(
            os.path.join(DATA_DIR, 'index.html.in'),
            encoding='utf-8') as f:
        index_template = f.read()

    replacements = {
        'TITLE': APP_NAME,
        'JS_LANGS': ','.join('"{}"'.format(l) for l in sorted(langs))
    }

    for k, v in replacements.items():
        index_template = index_template.replace(
            '@{}@'.format(k), v)

    with open_for_text_writing('index.html') as f:
        f.write(index_template)


def gen_unordered_list(tree):
    result = '<ul>\n'

    for element in tree:
        if isinstance(element, str):
            data, subtree = (element, None)
        else:
            data, subtree = element

        result += indent(
            '<li>\n{}\n{}</li>\n'.format(
                indent(data),
                indent(
                    gen_unordered_list(subtree) if subtree else '')))

    result += '</ul>\n'

    return result


def gen_lang_menu(langs, page_lang, page_suburl):
    translator = langs[page_lang]

    result = '<details>\n'
    result += indent('<summary>{}</summary>\n'.format(
        translator.gettext(N_('Language'))))

    lang_name_to_code = {}

    for lang_code, translator in langs.items():
        if lang_code == 'en':
            lang_name = 'English'
        else:
            lang_name = translator.gettext(GETTEXT_LANGUAGE_NAME_KEY)

        lang_name_to_code[lang_name] = lang_code

    items = []

    for lang_name, lang_code in sorted(lang_name_to_code.items()):
        if lang_code == page_lang:
            items.append('<b>{}</b>'.format(lang_name))
        else:
            url = lang_code + '/' + page_suburl
            items.append(
                '<a lang="{lang_code}" hreflang="{lang_code}" '
                'href="{}/{}">{}</a>'.format(
                    get_url_to_root(url),
                    srip_index_html(url),
                    lang_name,
                    lang_code=lang_code))

    result += indent('&emsp;'.join(items)) + '\n'
    result += '</details>\n'

    return result


def write_page(
        langs,
        page_lang,
        page_suburl,
        page_content_generator):
    page_url = page_lang + '/' + page_suburl
    root_url = get_url_to_root(page_url)

    title, body_data = page_content_generator(
        root_url, langs[page_lang])

    head_data = (
        '<title>{}</title>\n'
        '<meta charset="utf-8">\n'
        '<meta name="viewport" '
        'content="width=device-width, initial-scale=1">\n'
        '<link rel="icon" type="image/png" '
        'href="{root_url}/favicon.png">\n'
        '<link rel="stylesheet" href="{root_url}/style.css">\n'
        ).format(title, root_url=root_url)

    copyright_text = (
        '© {} <a href="mailto:{}?subject={}">{}</a>\n').format(
            APP_COPYRIGHT_YEAR,
            APP_AUTHOR_EMAIL,
            APP_NAME,
            APP_AUTHOR)

    page_path = url2pathname(page_url)
    os.makedirs(os.path.dirname(page_path), exist_ok=True)

    with open_for_text_writing(page_path) as f:
        f.write('<!DOCTYPE html>\n')
        f.write(
            '<!-- This document is automatically generated. -->\n')
        f.write('<html lang="{}">\n'.format(page_lang))
        f.write('<head>\n')
        f.write(indent(head_data))
        f.write('</head>\n')
        f.write('<body>\n')
        f.write(indent(body_data))
        f.write(indent(
            '<footer>\n{}</footer>\n'.format(indent(
                copyright_text
                + gen_lang_menu(langs, page_lang, page_suburl)))))
        f.write(
            '</body>\n'
            '</html>\n')


def gen_main_page_content(root_url, translator):
    logo = (
        '<img id="logo" src="{}/images/dpscreenocr.svg" alt="">\n'
        ).format(root_url)

    about_text = translator.gettext(N_(
        '<b>{app_name}</b> is a program to recognize text on the '
        'screen. Powered by <a href="https://en.wikipedia.org/wiki/'
        'Tesseract_(software)">Tesseract</a>, it supports more than '
        '100 languages and can split independent text blocks, such '
        'as columns. Read <a {manual_link}>the manual</a> for '
        'instructions on installing, configuring, and using the '
        'program.')).format(
            app_name=APP_NAME,
            # The manual is currently not translatable.
            manual_link='href="{}/manual.html"'.format(root_url))

    github_repo_file_url_base = (
        'https://raw.githubusercontent.com/danpla/dpscreenocr/v'
        + APP_VERSION)

    download_text = translator.gettext(N_(
        '<b>Download</b> version {version_number} ('
        '<a {changelog_link}>changes</a>, '
        '<a {license_link}>license</a>):')).format(
            version_number=APP_VERSION,
            changelog_link='href="{}/doc/changelog.txt"'.format(
                github_repo_file_url_base,
                APP_VERSION),
            license_link='href="{}/LICENSE.txt"'.format(
                github_repo_file_url_base,
                APP_VERSION))

    github_release_download_url_base = (
        'https://github.com/danpla/dpscreenocr/releases/download/'
        'v' + APP_VERSION)

    download_list = gen_unordered_list((
        (
            'GNU/Linux',
            (
                '<a href="https://software.opensuse.org//'
                    'download.html?project=home%3Adanpla&'
                    'package=dpscreenocr">Debian</a>',
                '<a href="https://launchpad.net/~daniel.p/'
                    '+archive/ubuntu/dpscreenocr">{}</a>'.format(
                        translator.gettext(
                            N_('Ubuntu and derivatives')))
            )
        ),
        translator.gettext(N_(
                'Windows 7 or newer: '
                '<a {installer_link}>installer</a> or '
                '<a {zip_archive_link}>ZIP archive</a>')).format(
            installer_link=(
                'href="{}/{}-{}-win32.exe"'.format(
                    github_release_download_url_base,
                    APP_NAME,
                    APP_VERSION)),
            zip_archive_link=(
                'href="{}/{}-{}-win32.zip"'.format(
                    github_release_download_url_base,
                    APP_NAME,
                    APP_VERSION))),
        '<a href="https://github.com/danpla/dpscreenocr/archive/'
            'v{}.tar.gz">{}</a>'.format(
                APP_VERSION,
                translator.gettext(N_('Source code (tar.gz)'))),
    ))

    contribute_text = translator.gettext(N_(
        '<b>Contribute</b> by taking part in '
        '<a {translations_link}>translation</a> or '
        '<a {development_link}>development</a>.')).format(
            translations_link=(
                'href="https://hosted.weblate.org/engage/'
                'dpscreenocr/"'),
            development_link=(
                'href="https://github.com/danpla/dpscreenocr"'))

    return (
        APP_NAME,
        logo
            + '<p>{}</p>\n'.format(about_text)
            + '<p>{}</p>\n'.format(download_text)
            + download_list
            + '<p>{}</p>\n'.format(contribute_text))


def write_pages(langs):
    write_root_index_page(langs)

    page_infos = (
        ('index.html', gen_main_page_content),
    )

    for lang_code in langs:
        for page_info in page_infos:
            suburl, content_generator = page_info
            write_page(
                langs,
                lang_code,
                suburl,
                content_generator)


def generate_website():
    mo_dir = os.path.join(tempfile.gettempdir(), 'website-tool-tmp')
    compile_po(mo_dir)
    write_pages(collect_langs(mo_dir))
    shutil.rmtree(mo_dir)


# We don't try to count % of translated messages in a PO file to
# decide if it makes sense to ship the translation, but a translated
# GETTEXT_LANGUAGE_NAME_KEY message is a bare minimum as it appears in
# the language selection menu. We thus put GETTEXT_LANGUAGE_NAME_KEY
# at the end of the script, making it the last message in PO files.
# This way, we can be more or less sure that at least some of the
# messages above are also ready.
#
# We intentionally use an awkward string for GETTEXT_LANGUAGE_NAME_KEY
# so that translators are not tempted to translate the word "English".

# Translators: This is the target language name to be shown in the
# language selection menu.
GETTEXT_LANGUAGE_NAME_KEY = N_('LANGUAGE_NAME')


def main():
    parser = argparse.ArgumentParser(description='Website tool')

    subparsers = parser.add_subparsers();

    update_po_parser = subparsers.add_parser(
        'update_po',
        help='Update PO files',
        description=(
            'update_po extracts translatable strings and merges them '
            'into existing PO files. To make a new PO file, add its '
            'code to the LOCALE_TO_BCP47 mapping of this script and '
            'run the command again.'))
    update_po_parser.add_argument(
        '--keep-pot',
        action='store_true',
        help='Don''t delete the generated en.pot file')
    update_po_parser.add_argument(
        '--no-fuzzy-matching',
        action='store_true',
        help='Don''t use fuzzy matching (same as in msgmerge)')
    update_po_parser.set_defaults(
        func=lambda args: update_po(
            no_fuzzy_matching=args.no_fuzzy_matching,
            keep_pot=args.keep_pot))

    generate_parser = subparsers.add_parser(
        'generate', help='Generate the website')
    generate_parser.set_defaults(
        func=lambda args: generate_website())

    args = parser.parse_args()

    if hasattr(args, 'func'):
        args.func(args)
    else:
        parser.print_help()


if __name__ == '__main__':
    main()
