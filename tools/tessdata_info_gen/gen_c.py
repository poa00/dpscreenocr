#!/usr/bin/env python3

from operator import itemgetter
import argparse
import json


def format_line(info, max_data_name_len):
    formatted_data_name = '"{}",'.format(info['data_name'])
    # Include 2 quotes and a comma
    formatted_data_name_fill_len = max_data_name_len + 3

    name = '; '.join(info['names'])
    name_infos = info['name_infos']
    if name_infos:
        name += ' ({})'.format(', '.join(name_infos))

    return '    {{{:<{}} N_("{}")}},\n'.format(
        formatted_data_name, formatted_data_name_fill_len, name)


def main():
    parser = argparse.ArgumentParser(description='Generate C source.')

    parser.add_argument(
        'info_file',
        help='Path to a JSON file generated by gen_info.py.')
    parser.add_argument('out_file', help='Output C file.')

    args = parser.parse_args()

    with open(args.info_file, 'r', encoding='utf-8') as f:
        infos = json.load(f)

    infos.sort(key=itemgetter('data_name'))

    with open(args.out_file, 'w', encoding='utf-8', newline='') as f:
        max_data_name_len = 0
        for info in infos:
            max_data_name_len = max(
                max_data_name_len, len(info['data_name']))

        for info in infos:
            f.write(format_line(info, max_data_name_len))


if __name__ == '__main__':
    main()
