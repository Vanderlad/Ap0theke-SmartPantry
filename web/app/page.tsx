'use client';

import { useEffect, useMemo, useRef, useState } from 'react';

type Ingredient = 'Pasta' | 'Sauce' | 'Rice' | 'Beans';
type Recipe = { name: string; amounts: Record<Ingredient, number> };
type ArduinoStatus = {
  type: 'status';
  selectedRecipe: number;
  items: Array<{ name: Ingredient; availableGrams: number }>;
};
type SerialPortLike = {
  open: (options: { baudRate: number }) => Promise<void>;
  close: () => Promise<void>;
  readable?: ReadableStream<Uint8Array> | null;
  writable?: WritableStream<Uint8Array> | null;
};
type SerialNavigator = Navigator & {
  serial?: { requestPort: () => Promise<SerialPortLike> };
};

const ingredients: Array<{ name: Ingredient; capacity: number; color: string }> = [
  { name: 'Pasta', capacity: 1000, color: '#f6c66b' },
  { name: 'Sauce', capacity: 700, color: '#e77262' },
  { name: 'Rice', capacity: 1000, color: '#c9d39b' },
  { name: 'Beans', capacity: 800, color: '#9c6b54' },
];

const recipes: Recipe[] = [
  { name: 'Tomato Pasta', amounts: { Pasta: 250, Sauce: 300, Rice: 0, Beans: 0 } },
  { name: 'Rice & Beans', amounts: { Pasta: 0, Sauce: 0, Rice: 300, Beans: 250 } },
  { name: 'Pantry Bowl', amounts: { Pasta: 0, Sauce: 150, Rice: 250, Beans: 150 } },
  { name: 'Pasta & Beans', amounts: { Pasta: 250, Sauce: 200, Rice: 0, Beans: 200 } },
  { name: 'Saucy Rice', amounts: { Pasta: 0, Sauce: 200, Rice: 350, Beans: 0 } },
  { name: 'Bean Pasta', amounts: { Pasta: 200, Sauce: 250, Rice: 0, Beans: 150 } },
];

const demoInventory: Record<Ingredient, number> = {
  Pasta: 620,
  Sauce: 180,
  Rice: 760,
  Beans: 120,
};

const grams = (value: number) => `${value.toLocaleString()} g`;

export default function Home() {
  const [selectedRecipe, setSelectedRecipe] = useState(0);
  const [inventory, setInventory] = useState(demoInventory);
  const [connectionMessage, setConnectionMessage] = useState('Demo mode');
  const [isConnected, setIsConnected] = useState(false);
  const [lastUpdated, setLastUpdated] = useState('using sample pantry data');
  const portRef = useRef<SerialPortLike | null>(null);
  const readingRef = useRef(false);

  const recipe = recipes[selectedRecipe];
  const recipeItems = ingredients.filter(({ name }) => recipe.amounts[name] > 0);
  const missingItems = recipeItems.filter(({ name }) => inventory[name] < recipe.amounts[name]);
  const shoppingList = ingredients.filter(({ name, capacity }) => inventory[name] / capacity < 0.25);
  const pantrySummary = useMemo(
    () => missingItems.length === 0 ? 'Ready to cook' : `${missingItems.length} ingredient${missingItems.length === 1 ? '' : 's'} short`,
    [missingItems.length],
  );

  function applyStatus(status: ArduinoStatus) {
    const updatedInventory = { ...demoInventory };
    status.items.forEach((item) => { updatedInventory[item.name] = item.availableGrams; });
    setInventory(updatedInventory);
    setSelectedRecipe(status.selectedRecipe);
    setLastUpdated('from your Arduino just now');
  }

  async function listenToArduino(port: SerialPortLike) {
    if (!port.readable || readingRef.current) return;
    readingRef.current = true;
    const reader = port.readable.getReader();
    const decoder = new TextDecoder();
    let incomingText = '';

    try {
      while (readingRef.current) {
        const { value, done } = await reader.read();
        if (done) break;
        incomingText += decoder.decode(value, { stream: true });
        const lines = incomingText.split(/\r?\n/);
        incomingText = lines.pop() ?? '';
        lines.forEach((line) => {
          try {
            const message = JSON.parse(line) as ArduinoStatus;
            if (message.type === 'status') applyStatus(message);
          } catch {
            // Human-readable Arduino messages are intentionally ignored here.
          }
        });
      }
    } catch {
      setConnectionMessage('Connection lost — switched to demo mode');
      setIsConnected(false);
    } finally {
      reader.releaseLock();
      readingRef.current = false;
    }
  }

  async function sendCommand(command: string) {
    const port = portRef.current;
    if (!port?.writable) return;
    const writer = port.writable.getWriter();
    try {
      await writer.write(new TextEncoder().encode(`${command}\n`));
    } finally {
      writer.releaseLock();
    }
  }

  async function connectArduino() {
    const serial = (navigator as SerialNavigator).serial;
    if (!serial) {
      setConnectionMessage('Web Serial needs Chrome or Edge on a computer');
      return;
    }
    try {
      const port = await serial.requestPort();
      await port.open({ baudRate: 9600 });
      portRef.current = port;
      setIsConnected(true);
      setConnectionMessage('Arduino connected');
      listenToArduino(port);
      await sendCommand('RECIPE:0');
    } catch {
      setConnectionMessage('Could not connect — demo mode is still available');
    }
  }

  async function disconnectArduino() {
    readingRef.current = false;
    const port = portRef.current;
    portRef.current = null;
    if (port) await port.close();
    setIsConnected(false);
    setConnectionMessage('Demo mode');
  }

  async function chooseRecipe(index: number) {
    setSelectedRecipe(index);
    if (isConnected) await sendCommand(`RECIPE:${index}`);
  }

  useEffect(() => () => { readingRef.current = false; }, []);

  return (
    <main>
      <header className="topbar">
        <a className="brand" href="#dashboard" aria-label="Smart Pantry dashboard">
          <span className="brand-mark" aria-hidden="true"><i /><i /><i /></span>
          <span>Smart Pantry</span>
        </a>
        <div className="connection">
          <span className={`connection-dot ${isConnected ? 'live' : ''}`} />
          <span>{connectionMessage}</span>
          <button className="text-button" onClick={isConnected ? disconnectArduino : connectArduino}>
            {isConnected ? 'Disconnect' : 'Connect Arduino'}
          </button>
        </div>
      </header>

      <section className="hero" id="dashboard">
        <p className="eyebrow">HOME INVENTORY, WITHOUT THE GUESSWORK</p>
        <h1>Cook from what you have.</h1>
        <p className="hero-copy">Choose a recipe and see exactly what is ready, what needs attention, and what to buy next.</p>
      </section>

      <section className="dashboard-grid" aria-label="Pantry dashboard">
        <article className="panel recipe-panel">
          <div className="panel-heading">
            <div><p className="eyebrow">MAKE TONIGHT</p><h2>Choose a recipe</h2></div>
            <span className={`status-pill ${missingItems.length ? 'warning' : 'ready'}`}>{pantrySummary}</span>
          </div>
          <div className="recipe-picker" role="list" aria-label="Recipes">
            {recipes.map((entry, index) => (
              <button className={`recipe-choice ${selectedRecipe === index ? 'selected' : ''}`} key={entry.name} onClick={() => chooseRecipe(index)} aria-pressed={selectedRecipe === index}>
                <span>{entry.name}</span><span className="recipe-arrow" aria-hidden="true">↗</span>
              </button>
            ))}
          </div>
          <div className="recipe-requirements">
            <div className="requirement-title"><span>{recipe.name}</span><span>Needed</span></div>
            {recipeItems.map(({ name, color }) => {
              const enough = inventory[name] >= recipe.amounts[name];
              return <div className="requirement-row" key={name}>
                <span className="ingredient-dot" style={{ background: color }} /><span>{name}</span><span>{grams(recipe.amounts[name])}</span>
                <span className={enough ? 'availability yes' : 'availability no'}>{enough ? 'Ready' : 'Short'}</span>
              </div>;
            })}
          </div>
        </article>

        <article className="panel inventory-panel">
          <div className="panel-heading"><div><p className="eyebrow">LIVE PANTRY</p><h2>Ingredient levels</h2></div><span className="muted">{lastUpdated}</span></div>
          <div className="inventory-list">
            {ingredients.map(({ name, capacity, color }) => {
              const percent = Math.round((inventory[name] / capacity) * 100);
              const required = recipe.amounts[name];
              const enough = required === 0 || inventory[name] >= required;
              return <div className="inventory-item" key={name}>
                <div className="inventory-label"><span><i className="ingredient-dot" style={{ background: color }} />{name}</span><strong>{grams(inventory[name])}</strong></div>
                <div className="meter" aria-label={`${name} is ${percent}% full`}><span style={{ width: `${percent}%`, background: color }} /></div>
                <div className="inventory-meta"><span>{percent}% of {grams(capacity)}</span>{required > 0 && <span className={enough ? 'availability yes' : 'availability no'}>{enough ? `Recipe needs ${grams(required)}` : `Need ${grams(required - inventory[name])} more`}</span>}</div>
              </div>;
            })}
          </div>
        </article>

        <aside className="side-stack">
          <article className="shopping-card">
            <p className="eyebrow">AUTOMATIC LIST</p><h2>Buy next</h2>
            {shoppingList.length ? <ul>{shoppingList.map(({ name }) => <li key={name}><span>{name}</span><span>{grams(inventory[name])} left</span></li>)}</ul> : <p className="empty-state">Your pantry is comfortably stocked.</p>}
            <button className="primary-button">View shopping list</button>
          </article>
          <article className="how-card"><p className="eyebrow">NEXT HARDWARE STEP</p><h2>Connect the real pantry</h2><p>When your Uno is connected by USB, use the button above. This dashboard will send recipe commands and read its JSON status messages.</p></article>
        </aside>
      </section>
    </main>
  );
}
