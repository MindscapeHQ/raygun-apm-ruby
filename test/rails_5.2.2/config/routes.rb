Rails.application.routes.draw do
  resources :posts
  get 'simple', to: 'simple#index'
  get 'simple_sleep', to: 'simple#index_sleep'
  get 'simple_http', to: 'simple#index_http'
  root to: 'simple#index'
end
