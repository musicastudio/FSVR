# Writes plugin/generated/contributors.txt: the repository's GitHub contributors other than its two collaborators
# (jameshansen and rgwan, credited by name in the About box above them) and bots, each by the name on their GitHub
# profile, else their handle, most contributions first, on one line. The plug-in embeds the file and the About box
# lists it. The build workflow runs this before every build, so a release credits whoever has contributed by then.
#   python tools/contributors.py [owner/repo]      (GITHUB_TOKEN, when set, lifts the API's rate limit)
# When GitHub cannot be reached the file is left as it is and the build goes on with it.
import json, os, sys, urllib.request
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / 'plugin' / 'generated' / 'contributors.txt'
COLLABORATORS = {'jameshansen', 'rgwan'}


def get(url):
    req = urllib.request.Request(url, headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'fsvr-contributors'})
    if os.environ.get('GITHUB_TOKEN'):
        req.add_header('Authorization', 'Bearer ' + os.environ['GITHUB_TOKEN'])
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


def names(people, profile):
    """The credit line: each contributor's profile name, else their handle, collaborators and bots left out."""
    out = []
    for p in people:
        login = p['login']
        if login.lower() in COLLABORATORS or p.get('type') == 'Bot' or login.endswith('[bot]'):
            continue
        name = (profile(login) or {}).get('name') or login
        if name not in out:
            out.append(name.strip())
    return ', '.join(out)


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else 'musicastudio/FSVR'
    try:
        people, page = [], 1
        while True:
            batch = get('https://api.github.com/repos/%s/contributors?per_page=100&page=%d' % (repo, page))
            people += batch
            if len(batch) < 100:
                break
            page += 1
        line = names(people, lambda login: get('https://api.github.com/users/' + login))
    except Exception as e:   # offline, or rate limited: keep what is there
        print('contributors: GitHub not reached (%s), %s left as it is' % (e, OUT.name))
        return
    OUT.write_text(line + '\n', encoding='utf-8', newline='\n')
    print('contributors:', line or '(none besides the collaborators)')


if __name__ == '__main__':
    if sys.argv[1:] == ['--check']:   # the filter, without the network
        people = [{'login': 'jameshansen'}, {'login': 'rgwan'}, {'login': 'mkruselj'}, {'login': 'dependabot[bot]', 'type': 'Bot'}, {'login': 'someone'}]
        line = names(people, lambda login: {'mkruselj': {'name': 'Mario Kruselj'}}.get(login))
        assert line == 'Mario Kruselj, someone', line
        print('contributors: --check passes')
    else:
        main()
