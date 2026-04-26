import { getClassColor } from './wow-colors.js';

export default function CharacterCard({ name, level, gender, race, cls, selected, onClick }) {
  const nameColor = getClassColor(cls);
  const nameStyle = nameColor ? `color: ${nameColor}` : undefined;

  return (
    <div
      class={`card ${selected ? 'border-primary' : ''}`}
      style={onClick ? 'cursor: pointer' : undefined}
      onClick={onClick}
    >
      <div class="card-body py-2 px-3">
        <div class="d-flex align-items-center gap-2">
          <h6 class="mb-0" style={nameStyle}>{name}</h6>
          <span class="badge bg-secondary">{level}</span>
        </div>
        <small class="text-body-secondary">{gender} {race} {cls}</small>
      </div>
    </div>
  );
}
