import { visit } from 'unist-util-visit';
import { readdirSync } from 'fs';
import { join } from 'path';
import { base } from '../site.js';

const ICONS_DIR = join(process.cwd(), 'public/img/params');
const AVAILABLE_ICONS = new Set(
  readdirSync(ICONS_DIR)
    .filter(f => f.endsWith('.svg'))
    .map(f => f.slice(0, -4))
);

const PARAMS = [
  'P_SHAPE',       'P_DISTORTION',  'P_PITCH',       'P_OCT',         'P_GLIDE',       'P_INTERVAL',
  'P_NOISE',       'P_RESO',        'P_DEGREE',      'P_SCALE',       'P_MICROTONE',   'P_COLUMN',
  'P_ENV_LVL1',   'P_ATTACK1',     'P_DECAY1',      'P_SUSTAIN1',    'P_RELEASE1',    'P_ROOT',
  'P_ENV_LVL2',   'P_ATTACK2',     'P_DECAY2',      'P_SUSTAIN2',    'P_RELEASE2',    'P_ENV2_UNUSED',
  'P_DLY_SEND',   'P_DLY_TIME',    'P_PING_PONG',   'P_DLY_WOBBLE',  'P_DLY_FEEDBACK','P_TEMPO',
  'P_RVB_SEND',   'P_RVB_TIME',    'P_SHIMMER',     'P_RVB_WOBBLE',  'P_RVB_UNUSED',  'P_SWING',
  'P_ARP_TGL',    'P_ARP_ORDER',   'P_ARP_CLK_DIV', 'P_ARP_CHANCE',  'P_ARP_EUC_LEN', 'P_ARP_OCTAVES',
  'P_LATCH_TGL',  'P_SEQ_ORDER',   'P_SEQ_CLK_DIV', 'P_SEQ_CHANCE',  'P_SEQ_EUC_LEN', 'P_GATE_LENGTH',
  'P_SCRUB',      'P_GR_SIZE',     'P_PLAY_SPD',    'P_SMP_STRETCH', 'P_SAMPLE',      'P_PATTERN',
  'P_SCRUB_JIT',  'P_GR_SIZE_JIT', 'P_PLAY_SPD_JIT','P_SMP_UNUSED1', 'P_SMP_UNUSED2', 'P_STEP_OFFSET',
  'P_A_SCALE',    'P_A_OFFSET',    'P_A_DEPTH',     'P_A_RATE',      'P_A_SHAPE',     'P_A_SYM',
  'P_B_SCALE',    'P_B_OFFSET',    'P_B_DEPTH',     'P_B_RATE',      'P_B_SHAPE',     'P_B_SYM',
  'P_X_SCALE',    'P_X_OFFSET',    'P_X_DEPTH',     'P_X_RATE',      'P_X_SHAPE',     'P_X_SYM',
  'P_Y_SCALE',    'P_Y_OFFSET',    'P_Y_DEPTH',     'P_Y_RATE',      'P_Y_SHAPE',     'P_Y_SYM',
  'P_SYN_LVL',   'P_SYN_WET_DRY', 'P_HPF',         'P_MIX_UNUSED1', 'P_SETTINGS1',   'P_VOLUME',
  'P_IN_LVL',    'P_IN_WET_DRY',  'P_SYS_UNUSED1', 'P_MIX_UNUSED2', 'P_SETTINGS2',   'P_MIX_WIDTH',
];

const PARAM_LABELS = {
  'P_SHAPE':        'Shape',
  'P_DISTORTION':   'Distortion',
  'P_PITCH':        'Pitch',
  'P_OCT':          'Octave',
  'P_GLIDE':        'Glide',
  'P_INTERVAL':     'Osc Interval',
  'P_NOISE':        'Noise',
  'P_RESO':         'Resonance',
  'P_DEGREE':       'Degree',
  'P_SCALE':        'Scale',
  'P_MICROTONE':    'Microtone',
  'P_COLUMN':       'Column',
  'P_ENV_LVL1':     'Sensitivity',
  'P_ATTACK1':      'Env 1 Attack',
  'P_DECAY1':       'Env 1 Decay',
  'P_SUSTAIN1':     'Env 1 Sustain',
  'P_RELEASE1':     'Env 1 Release',
  'P_ROOT':         'Root',
  'P_ENV_LVL2':     'Env 2 Level',
  'P_ATTACK2':      'Env 2 Attack',
  'P_DECAY2':       'Env 2 Decay',
  'P_SUSTAIN2':     'Env 2 Sustain',
  'P_RELEASE2':     'Env 2 Release',
  'P_DLY_SEND':     'Delay Send',
  'P_DLY_TIME':     'Delay Time',
  'P_PING_PONG':    'Delay Ping Pong',
  'P_DLY_WOBBLE':   'Delay Wobble',
  'P_DLY_FEEDBACK': 'Delay Feedback',
  'P_TEMPO':        'Tempo',
  'P_RVB_SEND':     'Reverb Send',
  'P_RVB_TIME':     'Reverb Time',
  'P_SHIMMER':      'Reverb Shimmer',
  'P_RVB_WOBBLE':   'Reverb Wobble',
  'P_SWING':        'Swing',
  'P_ARP_TGL':      'Arp Toggle',
  'P_ARP_ORDER':    'Arp Order',
  'P_ARP_CLK_DIV':  'Arp Clock Div',
  'P_ARP_CHANCE':   'Arp Chance',
  'P_ARP_EUC_LEN':  'Arp Euclid Len',
  'P_ARP_OCTAVES':  'Arp Octaves',
  'P_LATCH_TGL':    'Latch Toggle',
  'P_SEQ_ORDER':    'Seq Order',
  'P_SEQ_CLK_DIV':  'Seq Clock Div',
  'P_SEQ_CHANCE':   'Seq Chance',
  'P_SEQ_EUC_LEN':  'Seq Euclid Len',
  'P_GATE_LENGTH':  'Seq Gate Len',
  'P_SCRUB':        'Sample Scrub',
  'P_GR_SIZE':      'Sample Grain Size',
  'P_PLAY_SPD':     'Sample Play Spd',
  'P_SMP_STRETCH':  'Sample Stretch',
  'P_SAMPLE':       'Sample Id',
  'P_PATTERN':      'Pattern Id',
  'P_SCRUB_JIT':    'Sample Scrub Jitt',
  'P_GR_SIZE_JIT':  'Sample Size Jitt',
  'P_PLAY_SPD_JIT': 'Sample Speed Jitt',
  'P_STEP_OFFSET':  'Seq Step Offset',
  'P_A_SCALE':      'CV A Level',
  'P_A_OFFSET':     'LFO A Offset',
  'P_A_DEPTH':      'LFO A Depth',
  'P_A_RATE':       'LFO A Rate',
  'P_A_SHAPE':      'LFO A Shape',
  'P_A_SYM':        'LFO A Symmetry',
  'P_B_SCALE':      'CV B Level',
  'P_B_OFFSET':     'LFO B Offset',
  'P_B_DEPTH':      'LFO B Depth',
  'P_B_RATE':       'LFO B Rate',
  'P_B_SHAPE':      'LFO B Shape',
  'P_B_SYM':        'LFO B Symmetry',
  'P_X_SCALE':      'CV X Level',
  'P_X_OFFSET':     'LFO X Offset',
  'P_X_DEPTH':      'LFO X Depth',
  'P_X_RATE':       'LFO X Rate',
  'P_X_SHAPE':      'LFO X Shape',
  'P_X_SYM':        'LFO X Symmetry',
  'P_Y_SCALE':      'CV Y Level',
  'P_Y_OFFSET':     'LFO Y Offset',
  'P_Y_DEPTH':      'LFO Y Depth',
  'P_Y_RATE':       'LFO Y Rate',
  'P_Y_SHAPE':      'LFO Y Shape',
  'P_Y_SYM':        'LFO Y Symmetry',
  'P_SYN_LVL':      'Synth Level',
  'P_SYN_WET_DRY':  'Synth Wet/Dry',
  'P_HPF':          'HPF',
  'P_SETTINGS1':    'Settings',
  'P_VOLUME':       'Volume',
  'P_IN_LVL':       'Input Level',
  'P_IN_WET_DRY':   'Input Wet/Dry',
  'P_MIX_WIDTH':    'Stereo Width',
};

const LFO_START = PARAMS.indexOf('P_A_SCALE');
const LFO_END   = PARAMS.indexOf('P_Y_SYM');

function getIconName(name) {
  if (AVAILABLE_ICONS.has(name)) return name;

  const idx = PARAMS.indexOf(name);
  if (idx === -1) throw new Error(`[remark-param] Unknown param: ${name}`);

  const row = Math.floor(idx / 6);

  if (row % 2 === 1) {
    const above = PARAMS[idx - 6];
    if (above && AVAILABLE_ICONS.has(above)) return above;
  }

  if (idx >= LFO_START && idx <= LFO_END) {
    const col = idx % 6;
    const rowA = PARAMS[LFO_START + col];
    if (rowA && AVAILABLE_ICONS.has(rowA)) return rowA;
  }

  throw new Error(`[remark-param] No icon found for param: ${name}`);
}

export function remarkParam() {
  return (tree) => {
    visit(tree, 'textDirective', (node) => {
      const name = node.name.toUpperCase();
      if (!name.startsWith('P_')) return;
      if (!PARAM_LABELS[name]) throw new Error(`[remark-param] No label for param: ${name}`);
      const label = PARAM_LABELS[name].toLowerCase();
      const icon  = getIconName(name);
      node.data = {
        hName: 'span',
        hProperties: { class: 'param-ref' },
      };
      node.children = [
        { type: 'text', value: label },
        {
          type: 'image',
          url: `${base}/img/params/${icon}.svg`,
          alt: name,
          data: { hProperties: { class: 'param-img' } },
        },
      ];
    });
  };
}
