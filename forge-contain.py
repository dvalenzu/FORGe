#!/usr/bin/env python

"""
forge-contain.py

Helper for running FORGe within a container.  Helps to:
- set up the mounts
- set up needed environment variables (really not relevant for FORGe)
- form the proper docker or singularity command

It does so in a way that is fully compatible with either docker or
singularity.  Intended to work with the image generated by the Dockerfile in
this same directory.

/Users/langmead/FORGe_expt/small

--mount /tmp:/container-temporary \
        `pwd`:/container-wd \
        `pwd`/cache:/container-cache \
        `pwd`/output:/container-output \
-- \
--method hybrid \
--reference /container-wd/21_22_sm.fa \
--vars /container-wd/21_22_sm.1ksnp \
--cache-to /container-cache \
--window-size 100 \
--threads 8 \
--prune 15 \
--output /container-output \
--temp /container-temporary
"""

from __future__ import print_function
import os
import sys
import argparse


ENV_NAME = 'forge'  # set by env.yml
RANK_BIN = '/code/src/rank.py'  # set by Dockerfile
DOCKER_OPTIONS = '--cap-add=SYS_PTRACE --security-opt seccomp=unconfined'  # for gdb


def to_docker_env(env_list):
    return ' '.join(map(lambda x: '-e "%s"' % x, env_list))


def to_singularity_env(env_list):
    return 'export ' + '; '.join(map(lambda x: 'export ' + x, env_list)) + '; '


def to_docker_mounts(mount_list):
    return ' '.join(['-v %s:%s' % (src, dst) for src, dst in mount_list])


def to_singularity_mounts(mount_list):
    return ' '.join(['-B %s:%s' % (src, dst) for src, dst in mount_list])


def run_forge(mount_list, env_dict, pass_through, image, tag, docker=True):
    image_url = ':'.join([image, tag])
    env_list = ['%s=%s' % (k, v) for k, v in env_dict.items()]
    cmd_run = '/bin/bash -c "source activate %s && python %s %s"' %\
              (ENV_NAME, RANK_BIN, ' '.join(pass_through))
    if docker:
        mount_str = to_docker_mounts(mount_list)
        cmd = 'docker run %s %s %s %s %s' %\
              (DOCKER_OPTIONS, to_docker_env(env_list), mount_str, image_url, cmd_run)
    else:
        mount_str = to_singularity_mounts(mount_list)
        cmd = '%s singularity exec %s %s %s' % (to_singularity_env(env_list), mount_str, image_url, cmd_run)
    print(cmd, file=sys.stderr)
    ret = os.system(cmd)
    if ret != 0:
        raise RuntimeError('Command returned %d: "%s"' % (ret, cmd))


def go():
    if '--' not in sys.argv:
        raise ValueError('Must specify --')
    in_pass_through = False
    pass_through = []
    pass_through_i = None
    for i, arg in enumerate(sys.argv):
        if arg == '--':
            pass_through_i = i
            in_pass_through = True
        elif in_pass_through:
            pass_through.append(arg)
    assert pass_through_i is not None

    # Print file's docstring if -h is invoked
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mount', nargs='+', help='<src>:<dst>')
    parser.add_argument('--image', default='benlangmead/forge')
    parser.add_argument('--tag', default='latest')
    parser.add_argument('--system', default='docker', help='docker or singularity')
    args = parser.parse_args(sys.argv[1:pass_through_i])
    mounts = []
    for mount_str in args.mount:
        tokens = mount_str.split(':')
        mounts.append((tokens[0], tokens[1]))
    run_forge(mounts, {}, pass_through, args.image, args.tag, args.system == 'docker')


if __name__ == '__main__':
    go()
