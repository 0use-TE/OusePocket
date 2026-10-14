"""Static cross-file checks; does not compile firmware or prove hardware behavior."""
import json
import re
import ast
from pathlib import Path
import yaml

root = Path(__file__).resolve().parent.parent
app = root / 'components/brookesia_app_settings'
resources = app / 'package/res'
json_files = list(resources.rglob('*.json'))
for path in json_files:
    json.loads(path.read_text(encoding='utf-8'))

shell = root / 'components/brookesia_system_super'
for path in [app / 'idf_component.yml', shell / 'idf_component.yml',
             root / 'components/brookesia_gui_lvgl/idf_component.yml',
             root / 'components/brookesia_system_core/idf_component.yml',
             root / 'components/pocket_platform/idf_component.yml',
             root / 'examples/brookesia-minimal/main/idf_component.yml']:
    manifest = yaml.safe_load(path.read_text(encoding='utf-8'))
    for dependency in manifest['dependencies'].values():
        if isinstance(dependency, dict) and 'override_path' in dependency:
            override = (path.parent / dependency['override_path']).resolve()
            assert override.is_dir(), path
            # ESP-IDF resolves local overrides by their directory basename.
            if override.parent == root / 'components':
                dependency_name = next(name for name, spec in manifest['dependencies'].items()
                                       if spec is dependency)
                assert override.name == dependency_name.split('/')[-1], (dependency_name, override)

document = json.loads((resources / 'root.json').read_text(encoding='utf-8'))
for asset in document['assets']:
    assert (resources / asset).is_file(), asset

def walk(node):
    if isinstance(node, dict):
        yield node
        for child in node.values():
            yield from walk(child)
    elif isinstance(node, list):
        for child in node:
            yield from walk(child)

def check_document_actions(documents):
    """Literal event actions are unique per document, not per screen/node."""
    owners = {}
    def visit(value, owner):
        if isinstance(value, dict):
            for event in value.get('events', []):
                action = event.get('action', '')
                # Template references are resolved by the runtime, not this check.
                if not action or action.startswith('${'):
                    continue
                previous = owners.setdefault(action, owner)
                assert previous == owner, (
                    f"Document action must be unique: {action!r}: {previous} and {owner}")
            for key, child in value.items():
                visit(child, f'{owner}/{key}')
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, f'{owner}/{index}')
    for path, document in documents:
        visit(document, str(path))
    return len(owners)

action_counts = []
for directory in [resources, shell / 'resource/shell', root / 'examples/file-manager/src/res']:
    root_document = json.loads((directory / 'root.json').read_text(encoding='utf-8'))
    loaded_assets = set(root_document['assets'])
    for variant in root_document.get('variants', []):
        loaded_assets.update(variant.get('assets', []))
    action_counts.append(check_document_actions(
        (asset, json.loads((directory / asset).read_text(encoding='utf-8')))
        for asset in sorted(loaded_assets)
        if asset.startswith(('screens/', 'templates/'))))

cpp = (app / 'src/settings_app.cpp').read_text(encoding='utf-8')
handler = (app / 'src/app/lifecycle.ipp').read_text(encoding='utf-8')
actions = set()
for path in (resources / 'screens').glob('*.json'):
    screen = json.loads(path.read_text(encoding='utf-8'))
    for node in walk(screen):
        # IDs are scoped to their parent; different cards may each contain "title".
        siblings = node.get('children', [])
        ids = [child['id'] for child in siblings if isinstance(child, dict) and 'id' in child]
        assert len(ids) == len(set(ids)), f'Duplicate sibling ID in {path}'
        action = node.get('action', '')
        if action.startswith('ouse.'):
            actions.add(action)
            assert action in cpp and (action in handler or action.startswith('ouse.power.timeout')), f'Unconnected action: {action}'
assert len(actions) == 11, actions

# Validate the product shell overlay and its asset/action wiring as well.
shell_resources = shell / 'resource/shell'
for path in (shell / 'resource').rglob('*.json'):
    json.loads(path.read_text(encoding='utf-8'))
shell_document = json.loads((shell_resources / 'root.json').read_text(encoding='utf-8'))
for asset in shell_document['assets']:
    assert (shell_resources / asset).is_file(), asset
overlay = json.loads((shell_resources / 'screens/overlay.json').read_text(encoding='utf-8'))
quick = next(child for child in overlay['children'] if child['id'] == 'quick_controls')
assert quick['commonProps']['hidden'] and quick['commonProps']['clickable']
assert overlay['children'][-1]['id'] in {'quick_controls', 'recents'}
quick_actions = {event['action'] for node in walk(quick) for event in node.get('events', [])}
quick_handlers = (shell / 'src/shell_quick_controls.cpp').read_text(encoding='utf-8')
quick_subscriptions = (shell / 'src/shell_display.cpp').read_text(encoding='utf-8')
for action in quick_actions:
    assert action in quick_handlers and action in quick_subscriptions, action
assert len(quick_actions) == 4
recents = next(child for child in overlay['children'] if child['id'] == 'recents')
recent_actions = {event['action'] for node in walk(recents) for event in node.get('events', [])}
recent_handler = (shell / 'src/shell_recent_tasks.cpp').read_text(encoding='utf-8')
assert recent_actions == {'ouse.recents.native'}, recent_actions
assert 'ouse.recents.native' in quick_subscriptions and 'ouse.recents.native' in recent_handler
assert recents['commonProps']['scrollable'] is False
assert not any(child['id'] == 'cards' for child in recents['children'])
for node in walk(quick):
    if node.get('type') == 'slider':
        limits = node['rangeProps']
        assert limits['min'] <= limits['value'] <= limits['max']
    if node.get('type') == 'label':
        assert node['commonProps']['clickable'] is False, 'Labels must not intercept button clicks'

# Check new styles against the real GUI parser; JSON syntax alone misses ignored keys.
style_parser = (root / '.reference/esp-brookesia/gui/brookesia_gui_interface/src/parser_style.cpp').read_text(encoding='utf-8')
style_keys = set(re.findall(r'parse_optional_\w+\("(\w+)"', style_parser)) | {'font', 'font_size', 'text_align'}
def camel_case(name):
    first, *rest = name.split('_')
    return first + ''.join(part.capitalize() for part in rest)
supported_styles = {camel_case(key) for key in style_keys}
for node in walk(quick):
    assert set(node.get('style', {})) <= supported_styles, node.get('id')

file_resources = root / 'examples/file-manager/src/res'
file_manifest = json.loads((file_resources.parent / 'manifest.json').read_text(encoding='utf-8'))
assert file_manifest['services'] == [{'name': 'Pocket', 'version': '1.0.0'}]
assert '.version="1.0.0"' in (root / 'components/pocket_platform/pocket_platform.cpp').read_text(encoding='utf-8')
# Fonts use sp; layout dimensions use dp. Check every product resource, including
# state/part styles, rather than only the newly added screens. References are
# resolved by Brookesia before parsing and are excluded from this literal check.
font_fields = {'fontSize', 'font_size', 'imageFontSize', 'image_font_size'}
font_count = 0
for directory in [resources, shell / 'resource', file_resources]:
    for path in directory.rglob('*.json'):
        for node in walk(json.loads(path.read_text(encoding='utf-8'))):
            for field in font_fields & node.keys():
                value = node[field]
                if isinstance(value, str) and value.startswith('${'):
                    continue
                # The current parser also accepts integer pixel values.
                assert (type(value) is int or
                        isinstance(value, str) and re.fullmatch(
                            r'\s*[+-]?(?:\d+(?:\.\d*)?|\.\d+)sp', value)), (
                            path, field, value, 'Font size must use sp units')
                font_count += 1

file_root = json.loads((file_resources / 'root.json').read_text(encoding='utf-8'))
for asset in file_root['assets']:
    assert (file_resources / asset).is_file(), asset
file_profile = json.loads((file_resources / 'profile.json').read_text(encoding='utf-8'))
file_flow = json.loads((file_resources / 'flows/main.json').read_text(encoding='utf-8'))
assert file_profile['screen_flows'][0]['screen_flow'] == file_flow['id']
assert file_flow['initial'] in file_flow['screens']
for path in [file_resources / 'screens/main.json', resources / 'screens/power.json']:
    for node in walk(json.loads(path.read_text(encoding='utf-8'))):
        assert set(node.get('style', {})) <= supported_styles, (path, node.get('id'))
        assert set(node.get('style', {})) & {'bgOpacity', 'rowGap'} == set()
for node in walk(overlay):
    assert set(node.get('style', {})) <= supported_styles, node.get('id')
stage = (root / 'examples/brookesia-minimal/main/CMakeLists.txt').read_text(encoding='utf-8')
assert 'brookesia_stage_runtime_app_package(' not in stage and 'counter' not in stage

for locale in ['en', 'zh_CN']:
    bundle = json.loads((resources / f'i18n/{locale}.json').read_text(encoding='utf-8'))
    for key in ['ouse.density.restart']:
        assert key in bundle['strings'], (locale, key)
    assert all('/ble_' not in update['path'] for update in bundle['updates'])
    # These product labels use the "text" store key; official settings labels
    # usually use "labelProps.text". Validate the actual declarative target.
    for update in bundle['updates']:
        if not (update['path'].startswith('/power/') or
                update['path'] == '/settings_home/page/power/title'):
            continue
        parts = update['path'].strip('/').split('/')
        node = json.loads((resources / f'screens/{parts[0]}.json').read_text(encoding='utf-8'))
        for part in parts[1:]:
            node = next(child for child in node['children'] if child['id'] == part)
        assert update['key'] in node.get('bindings', {}).values(), (locale, update)

config_dir = root / 'examples/brookesia-minimal'
default_config = (config_dir / 'sdkconfig.defaults').read_text(encoding='utf-8')
sources = [root / '.reference/esp-hosted-2.12.3/Kconfig']
if sources[0].exists():
    hosted = sources[0].read_text(encoding='utf-8')
    hosted_names = set(re.findall(r'^\s*config\s+(ESP_HOSTED_\w+)', hosted, re.M))
    used_names = set(re.findall(r'^CONFIG_(ESP_HOSTED_\w+)=', default_config, re.M))
    assert used_names <= hosted_names, used_names - hosted_names

config_source = ast.parse((root / 'tools/enable-device-config.py').read_text(encoding='utf-8'))
config_dict = next(node.value for node in config_source.body if isinstance(node, ast.Assign)
                   and any(isinstance(target, ast.Name) and target.id == 'config' for target in node.targets))
modified_keys = {'CONFIG_' + key for key in ast.literal_eval(config_dict)}
for name in ['sdkconfig.defaults', 'sdkconfig']:
    path = config_dir / name
    if not path.exists():
        continue
    text = path.read_text(encoding='utf-8')
    keys = re.findall(r'^(?:# )?(CONFIG_[A-Z0-9_]+)(?:=| is not set)', text, re.M)
    # ESP-IDF may retain duplicate legacy Wi-Fi aliases; check the fields we manage.
    relevant_keys = [key for key in keys if key in modified_keys]
    assert len(relevant_keys) == len(set(relevant_keys)), f'Duplicate managed config in {path}'

print(f'PASS: {len(json_files)} JSON resources, dependency paths, root assets, '
      f'{len(actions)} settings and {len(quick_actions)} quick-control actions, '
      f'overlay styles, {font_count} font sizes, {sum(action_counts)} unique literal actions, '
      f'2 locales and configuration consistency')
