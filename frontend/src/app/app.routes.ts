import { Routes } from '@angular/router';

export const routes: Routes = [
  { path: '', redirectTo: 'search', pathMatch: 'full' },
  { path: 'search', loadComponent: () => import('./pages/search/search').then(m => m.SearchPage) },
  { path: 'emails/:id', loadComponent: () => import('./pages/email/email').then(m => m.EmailPage) },
  { path: 'threads/:tid', loadComponent: () => import('./pages/thread/thread').then(m => m.ThreadPage) },
  { path: 'backup', loadComponent: () => import('./pages/backup/backup').then(m => m.BackupPage) },
  { path: '**', redirectTo: 'search' },
];
