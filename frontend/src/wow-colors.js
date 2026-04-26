export const CLASS_COLORS = {
  'Death Knight': '#C41E3B',
  'Druid': '#FF7C0A',
  'Hunter': '#AAD372',
  'Mage': '#68CCEF',
  'Paladin': '#F48CBA',
  'Priest': '#FFFFFF',
  'Rogue': '#FFF468',
  'Shaman': '#2359FF',
  'Warlock': '#9382C9',
  'Warrior': '#C69B6D',
};

export function getClassColor(cls) {
  return CLASS_COLORS[cls] || null;
}

const HORDE_RACES = ['Orc', 'Undead', 'Tauren', 'Troll', 'Blood Elf'];
const ALLIANCE_RACES = ['Human', 'Dwarf', 'Night Elf', 'Gnome', 'Draenei'];

export function getFaction(race) {
  if (HORDE_RACES.includes(race)) return 'horde';
  if (ALLIANCE_RACES.includes(race)) return 'alliance';
  return null;
}
